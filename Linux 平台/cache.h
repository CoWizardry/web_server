// cache.h
#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>
#include <time.h>

#define MAX_CACHE_SIZE 128
#define CACHE_TTL 1800  // 30 minutes in seconds

typedef struct {
    char path[512];
    char *content;
    size_t size;
    time_t last_accessed;
} cache_entry_t;

void init_cache();
void cleanup_cache();
cache_entry_t* find_in_cache(const char *path);
void add_to_cache(const char *path, const char *content, size_t size);
void evict_expired_cache();

#endif // CACHE_H
