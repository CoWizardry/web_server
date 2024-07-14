#include "log.h"
#include "request.h"
#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <getopt.h>

#define DEFAULT_THREAD_POOL_SIZE 10
#define DEFAULT_MAX_QUEUE_SIZE 500
#define DEFAULT_PORT 8080
// #define DEFAULT_MAX_CONNECTION_SIZE 510

int port = DEFAULT_PORT,
    thread_pool_size = DEFAULT_THREAD_POOL_SIZE,
    max_queue_size = DEFAULT_MAX_QUEUE_SIZE;

pthread_t *thread_pool;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t connection_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t connection_cond = PTHREAD_COND_INITIALIZER;
int *client_queue;
int queue_size = 0;
int connection_count = 0;

void* worker_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&queue_lock);
        while (queue_size == 0) {
            pthread_cond_wait(&queue_cond, &queue_lock);
        }
        int client_socket = client_queue[--queue_size];
        pthread_mutex_unlock(&queue_lock);

        handle_request(client_socket);
    }
    return NULL;
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [--port <port>] [--threads <thread_pool_size>] [--queues <client_queue>] [--webroot <web_root>]\n"
                    "       where options include:\n"
                    "           <port>: the port number to listen on (default: 8080)\n"
                    "           <thread_pool_size>: the number of threads in the thread pool (default: 50)\n"
                    "           <client_queue>: the size of the client request queue (default: 500)\n"
                    "           <web_root>: the root directory of the web server files (default: current directory)\n",
                    prog_name);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    thread_pool = (pthread_t *)malloc(DEFAULT_THREAD_POOL_SIZE * sizeof(pthread_t));
    client_queue = (int *)malloc(DEFAULT_MAX_QUEUE_SIZE * sizeof(int));

    if (thread_pool == NULL || client_queue == NULL) {
        log_message(ERROR, "Memory allocation failed");
        return 1;
    }

    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"threads", required_argument, 0, 't'},
        {"queues", required_argument, 0, 'q'},
        {"webroot", required_argument, 0, 'w'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "p:t:q:w:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 't':
                thread_pool_size = atoi(optarg);
                pthread_t *temp0 = (pthread_t *)realloc(thread_pool, thread_pool_size * sizeof(pthread_t));
                if (temp0 == NULL) {
                    free(thread_pool);
                    log_message(ERROR, "Memory allocation for thread_pool failed");
                    return 1;
                }
                thread_pool = temp0;
                break;
            case 'q':
                max_queue_size = atoi(optarg);
                int *temp1 = (int *)realloc(client_queue, max_queue_size * sizeof(int));
                if (temp1 == NULL) {
                    free(client_queue);
                    log_message(ERROR, "Memory allocation for client_queue failed");
                    return 1;
                }
                client_queue = temp1;
                break;
            case 'w':
                web_root = optarg;
                break;
            default:
                print_usage(argv[0]);
        }
    }

    // 如果没有指定参数名，则根据位置解析参数
    if (optind < argc) {
        if (argc - optind == 1) {
            port = atoi(argv[optind]);
        } else if (argc - optind == 2) {
            port = atoi(argv[optind]);
            thread_pool_size = atoi(argv[optind + 1]);
            pthread_t *temp = (pthread_t *)realloc(thread_pool, thread_pool_size * sizeof(pthread_t));
            if (temp == NULL) {
                free(thread_pool);
                log_message(ERROR, "Memory allocation for thread_pool failed");
                return 1;
            }
            thread_pool = temp;
        } else if (argc - optind == 3) {
            port = atoi(argv[optind]);
            thread_pool_size = atoi(argv[optind + 1]);
            pthread_t *temp0 = (pthread_t *)realloc(thread_pool, thread_pool_size * sizeof(pthread_t));
            if (temp0 == NULL) {
                free(thread_pool);
                log_message(ERROR, "Memory allocation for thread_pool failed");
                return 1;
            }
            thread_pool = temp0;
            max_queue_size = atoi(argv[optind + 2]);
            int *temp1 = (int *)realloc(client_queue, max_queue_size * sizeof(int));
            if (temp1 == NULL) {
                free(client_queue);
                log_message(ERROR, "Memory allocation for client_queue failed");
                return 1;
            }
            client_queue = temp1;
        } else if (argc - optind == 4) {
            port = atoi(argv[optind]);
            thread_pool_size = atoi(argv[optind + 1]);
            pthread_t *temp0 = (pthread_t *)realloc(thread_pool, thread_pool_size * sizeof(pthread_t));
            if (temp0 == NULL) {
                free(thread_pool);
                log_message(ERROR, "Memory allocation for thread_pool failed");
                return 1;
            }
            thread_pool = temp0;
            max_queue_size = atoi(argv[optind + 2]);
            int *temp1 = (int *)realloc(client_queue, max_queue_size * sizeof(int));
            if (temp1 == NULL) {
                free(client_queue);
                log_message(ERROR, "Memory allocation for client_queue failed");
                return 1;
            }
            client_queue = temp1;
            web_root = argv[optind + 3];
        } else {
            print_usage(argv[0]);
        }
    }

    init_log();
    init_cache();

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        log_message(ERROR, "Could not create socket: %s", strerror(errno));
        return 1;
    }

    int opt_ = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt_, sizeof(opt_)) < 0) {
        log_message(ERROR, "setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_message(ERROR, "Bind failed: %s", strerror(errno));
        return 1;
    }

    if (listen(server_socket, thread_pool_size) < 0) {
        log_message(ERROR, "Listen failed: %s", strerror(errno));
        return 1;
    }

    printf("Server listening on port %d\n", port);
    log_message(INFO, "Server started on port %d", port);

    for (int i = 0; i < thread_pool_size; i++) {
        if (pthread_create(&thread_pool[i], NULL, worker_thread, NULL) != 0) {
            log_message(ERROR, "Failed to create thread %d: %s", i, strerror(errno));
        } else {
            log_message(INFO, "Thread %d created successfully", i);
        }
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        pthread_mutex_lock(&connection_lock);
        while (connection_count >= max_queue_size) {
            pthread_cond_wait(&connection_cond, &connection_lock);
        }
        pthread_mutex_unlock(&connection_lock);

        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            log_message(ERROR, "Accept failed: %s", strerror(errno));
            continue;
        }

        pthread_mutex_lock(&connection_lock);
        connection_count++;
        pthread_mutex_unlock(&connection_lock);

        // 获取客户端IP地址并记录日志
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        log_message(INFO, "New connection from %s", client_ip);

        pthread_mutex_lock(&queue_lock);
        if (queue_size < max_queue_size) {
        client_queue[queue_size++] = client_socket;
        pthread_cond_signal(&queue_cond);
        } else {
            close(client_socket);  // 队列已满，拒绝新连接
            log_message(WARN, "Queue full, rejected new connection from %s", client_ip);
        }
        pthread_mutex_unlock(&queue_lock);
    }

    free(thread_pool);
    free(client_queue);
    cleanup_cache();
    close_log();
    close(server_socket);
    return 0;
}
