#include "log.h"
#include <stdio.h>
#include <time.h>
#include <pthread.h>

FILE *log_file;
pthread_mutex_t log_lock;

void init_log() {
    log_file = fopen("webserver.log", "a");
    pthread_mutex_init(&log_lock, NULL);
}

void log_message(const char *message) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    pthread_mutex_lock(&log_lock);
    fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec, message);
    fflush(log_file);
    pthread_mutex_unlock(&log_lock);
}

void close_log() {
    pthread_mutex_lock(&log_lock);
    fclose(log_file);
    pthread_mutex_unlock(&log_lock);
    pthread_mutex_destroy(&log_lock);
}
