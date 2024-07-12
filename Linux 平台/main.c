// main.c
#include "log.h"
#include "request.h"
#include "cache.h"
#include <stdlib.h>
#include <string.h>
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

#define THREAD_POOL_SIZE 50  // 增加线程池大小
#define MAX_QUEUE_SIZE 500  // 增加最大队列大小
#define PORT 8080
#define BUFFER_SIZE 4096
#define WEB_ROOT "."

pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
int client_queue[MAX_QUEUE_SIZE];
int queue_size = 0;

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

int main() {
    init_log();
    init_cache();

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Could not create socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_socket, THREAD_POOL_SIZE) < 0) {
        perror("Listen failed");
        return 1;
    }

    printf("Server listening on port %d\n", PORT);
    log_message("Server started");

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&thread_pool[i], NULL, worker_thread, NULL);
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        pthread_mutex_lock(&queue_lock);
        if (queue_size < MAX_QUEUE_SIZE) {
            client_queue[queue_size++] = client_socket;
            pthread_cond_signal(&queue_cond);
        } else {
            close(client_socket);  // 队列已满，拒绝新连接
            log_message("Queue full, rejected new connection");
        }
        pthread_mutex_unlock(&queue_lock);
    }

    cleanup_cache();
    close_log();
    close(server_socket);
    return 0;
}
