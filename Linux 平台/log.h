#ifndef LOG_H
#define LOG_H

typedef enum {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
} LogLevel;

void init_log();
void close_log();
void log_message(LogLevel level, const char *format, ...);
const char* get_log_level_string(LogLevel level);

#endif // LOG_H
