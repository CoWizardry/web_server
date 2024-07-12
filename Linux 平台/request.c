#include "request.h"
#include "log.h"
#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>

#define BUFFER_SIZE 4096
#define WEB_ROOT "."

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

void send_file(int client_socket, const char *path, int keep_alive) {
    cache_entry_t *cached_entry = find_in_cache(path);
    if (cached_entry != NULL) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %zu\r\n"
                 "Cache-Control: max-age=3600\r\n"
                 "Connection: %s\r\n"
                 "\r\n", cached_entry->size, keep_alive ? "keep-alive" : "close");
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
    if (content == NULL) {
        close(file);
        send_404(client_socket);
        log_message("Memory allocation failed");
        return;
    }

    if (read(file, content, file_size) > 0) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %zu\r\n"
                 "Cache-Control: max-age=3600\r\n"
                 "Connection: %s\r\n"
                 "\r\n", file_size, keep_alive ? "keep-alive" : "close");
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

void handle_request(int client_socket) {
    char buffer[BUFFER_SIZE];
    int keep_alive = 0;

    while (1) {
        int read_size = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (read_size <= 0) {
            break;
        }

        buffer[read_size] = '\0';
        printf("Received request:\n%s\n", buffer);
        log_message("Received request");

        char method[16], encoded_path[256];
        sscanf(buffer, "%15s %255s", method, encoded_path);

        char decoded_path[256];
        url_decode(decoded_path, encoded_path);
        removeQueryString(decoded_path);

        if (strstr(buffer, "Connection: keep-alive")) {
            keep_alive = 1;
        } else {
            keep_alive = 0;
        }

        if (strcmp(method, "GET") == 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s%s", WEB_ROOT, decoded_path);
            if (strlen(full_path) > 0 && full_path[strlen(full_path) - 1] == '/') {
                strncat(full_path, "index.html", sizeof(full_path) - strlen(full_path) - 1);
            }
            evict_expired_cache();
            send_file(client_socket, full_path, keep_alive);
        } else {
            send_404(client_socket);
        }

        if (!keep_alive) {
            break;
        }
    }

    close(client_socket);
    log_message("Client socket closed");
}
