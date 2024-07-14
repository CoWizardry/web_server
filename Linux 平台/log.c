#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#define LOG_FILE_PREFIX "server"
#define LOG_FILE_SUFFIX ".log"
static FILE *logfile;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static char current_log_file[256];

void open_log_file() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char date[20];
    strftime(date, sizeof(date), "%Y-%m-%d", tm_info);

    snprintf(current_log_file, sizeof(current_log_file), "%s_%s%s", LOG_FILE_PREFIX, date, LOG_FILE_SUFFIX);

    logfile = fopen(current_log_file, "a");
    if (!logfile) {
        perror("Failed to open log file");
    }
}

void close_log_file() {
    if (logfile) {
        fclose(logfile);
        logfile = NULL;
    }
}

void init_log() {
    open_log_file();
}

void close_log() {
    close_log_file();
}

const char* get_log_level_string(LogLevel level) {
    switch (level) {
        case DEBUG: return "DEBUG";
        case INFO: return "INFO";
        case WARN: return "WARN";
        case ERROR: return "ERROR";
        case FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

void log_message(LogLevel level, const char *format, ...) {
    pthread_mutex_lock(&log_lock);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char date[20];
    strftime(date, sizeof(date), "%Y-%m-%d", tm_info);

    if (logfile && strcmp(current_log_file, date) != 0) {
        close_log_file();
        open_log_file();
    }

    if (!logfile) {
        pthread_mutex_unlock(&log_lock);
        return;
    }

    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(logfile, "[%s] [%s] ", time_str, get_log_level_string(level));

    va_list args;
    va_start(args, format);
    vfprintf(logfile, format, args);
    va_end(args);

    fprintf(logfile, "\n");
    fflush(logfile);

    pthread_mutex_unlock(&log_lock);
}
