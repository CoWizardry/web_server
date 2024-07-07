#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <wchar.h>  
#include <process.h>
#include <conio.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 4096
#define WEB_ROOT L"."
#define THREAD_POOL_SIZE 8

FILE *log_file;

void log_message(const char *message) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec, message);
    fflush(log_file);
}

void send_404(SOCKET client_socket) {
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

void send_file(SOCKET client_socket, const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        send_404(client_socket);
        return;
    }

    LARGE_INTEGER file_size;
    GetFileSizeEx(file, &file_size);

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer),
             "HTTP/1.1 200 OK\r\n"
             "Content-Length: %lld\r\n"
             "Connection: close\r\n"
             "\r\n", file_size.QuadPart);
    send(client_socket, buffer, strlen(buffer), 0);

    DWORD bytes_read;
    while (ReadFile(file, buffer, sizeof(buffer), &bytes_read, NULL) && bytes_read > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    CloseHandle(file);
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

void handle_client(SOCKET client_socket) {
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
        wchar_t wide_path[512];
        MultiByteToWideChar(CP_UTF8, 0, decoded_path, -1, wide_path, sizeof(wide_path) / sizeof(wide_path[0]));

        if (strcmp(method, "GET") == 0) {
            wchar_t full_path[512];
            swprintf(full_path, sizeof(full_path) / sizeof(full_path[0]), L"%s%s", WEB_ROOT, wide_path);
            if (wcslen(full_path) > 0 && full_path[wcslen(full_path) - 1] == L'/') {
                wcscat(full_path, L"index.html");
            }
            send_file(client_socket, full_path);
        } else {
            send_404(client_socket);
        }
    }

    closesocket(client_socket);
    log_message("Client socket closed");
}

typedef struct {
    SOCKET client_socket;
    HANDLE thread_handle;
    int available;
    HANDLE condition;
} worker_t;

worker_t workers[THREAD_POOL_SIZE];
CRITICAL_SECTION pool_lock;

unsigned __stdcall worker_function(void *arg) {
    worker_t *worker = (worker_t *)arg;
    while (1) {
        WaitForSingleObject(worker->condition, INFINITE);
        handle_client(worker->client_socket);
        worker->available = 0;
    }
    return 0;
}

void init_thread_pool() {
    InitializeCriticalSection(&pool_lock);
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        workers[i].available = 0;
        workers[i].condition = CreateEvent(NULL, FALSE, FALSE, NULL);
        workers[i].thread_handle = (HANDLE)_beginthreadex(NULL, 0, worker_function, &workers[i], 0, NULL);
    }
}

worker_t *get_available_worker() {
    worker_t *available_worker = NULL;
    EnterCriticalSection(&pool_lock);
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        if (!workers[i].available) {
            available_worker = &workers[i];
            break;
        }
    }
    LeaveCriticalSection(&pool_lock);
    return available_worker;
}

int main() {
    log_file = fopen("log.txt", "a");
    if (!log_file) {
        printf("Could not open log file\n");
        return 1;
    }

    WSADATA wsa;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed. Error Code : %d", WSAGetLastError());
        log_message("WSAStartup failed");
        return 1;
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        printf("Could not create socket : %d", WSAGetLastError());
        log_message("Could not create socket");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed with error code : %d", WSAGetLastError());
        log_message("Bind failed");
        closesocket(server_socket);
        return 1;
    }

    if (listen(server_socket, 10) == SOCKET_ERROR) {
        printf("Listen failed with error code : %d", WSAGetLastError());
        log_message("Listen failed");
        closesocket(server_socket);
        return 1;
    }

    init_thread_pool();
    log_message("Thread pool initialized");
    log_message("Server listening");

    printf("Server listening on port %d\n", PORT);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) {
            printf("Accept failed with error code : %d", WSAGetLastError());
            log_message("Accept failed");
            continue;
        }

        worker_t *worker = get_available_worker();
        if (worker) {
            worker->client_socket = client_socket;
            worker->available = 1;
            SetEvent(worker->condition);
        } else {
            handle_client(client_socket);
        }
    }

    closesocket(server_socket);
    WSACleanup();
    DeleteCriticalSection(&pool_lock);
    fclose(log_file);
    return 0;
}
