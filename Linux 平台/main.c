// main.c
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

#define THREAD_POOL_SIZE 10
#define PORT 8080
#define BUFFER_SIZE 4096
#define WEB_ROOT "."

FILE *log_file;

void log_message(const char *message) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec, message);
    fflush(log_file);
}

void send_404(int client_socket) {
    const char *response =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>404 Not Found</h1></body></html>";
    send(client_socket, response, strlen(response), 0);
    log_message("Sent 404 Not Found");
}

void removeQueryString(char *filePath) {
    char *questionMark = strchr(filePath, '?');
    if (questionMark != NULL) {
        *questionMark = '\0';
    }
}

void send_file(int client_socket, const char *path) {
    cache_entry_t *cached_entry = find_in_cache(path);
    if (cached_entry != NULL) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n", cached_entry->size);
        send(client_socket, buffer, strlen(buffer), 0);
        send(client_socket, cached_entry->content, cached_entry->size, 0);
        log_message("File sent from cache");
        return;
    }

    int file = open(path, O_RDONLY);
    if (file == -1) {
        send_404(client_socket);
        return;
    }

    struct stat file_stat;
    fstat(file, &file_stat);
    size_t file_size = file_stat.st_size;

    char *content = malloc(file_size);
    if (read(file, content, file_size) > 0) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n", file_size);
        send(client_socket, buffer, strlen(buffer), 0);
        send(client_socket, content, file_size, 0);
        add_to_cache(path, content, file_size);
    } else {
        send_404(client_socket);
    }
    free(content);
    close(file);
    log_message("File sent successfully");
}

void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    int read_size = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (read_size > 0) {
        buffer[read_size] = '\0';
        printf("Received request:\n%s\n", buffer);
        log_message("Received request");

        char method[16], encoded_path[256];
        sscanf(buffer, "%s %s", method, encoded_path);

        char decoded_path[256];
        url_decode(decoded_path, encoded_path);
        removeQueryString(decoded_path);

        if (strcmp(method, "GET") == 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s%s", WEB_ROOT, decoded_path);
            if (strlen(full_path) > 0 && full_path[strlen(full_path) - 1] == '/') {
                strcat(full_path, "index.html");
            }
            evict_expired_cache();
            send_file(client_socket, full_path);
        } else {
            send_404(client_socket);
        }
    }

    close(client_socket);
    log_message("Client socket closed");
}

typedef struct {
    int client_socket;
} client_data_t;

void* worker_thread(void *arg) {
    client_data_t *data = (client_data_t *)arg;
    handle_client(data->client_socket);
    free(data);
    return NULL;
}

int main() {
    init_cache();
    log_file = fopen("webserver.log", "a");

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

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        client_data_t *data = (client_data_t *)malloc(sizeof(client_data_t));
        data->client_socket = client_socket;
        pthread_t thread;
        pthread_create(&thread, NULL, worker_thread, data);
        pthread_detach(thread);
    }

    cleanup_cache();
    fclose(log_file);
    close(server_socket);
    return 0;
}
