#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
void log_message(const char *message);

static cache_entry_t cache[MAX_CACHE_SIZE];
static int cache_count = 0;
static pthread_mutex_t cache_lock;

void init_cache() {
    pthread_mutex_init(&cache_lock, NULL);
    cache_count = 0;
}

void cleanup_cache() {
    pthread_mutex_lock(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        free(cache[i].content);
    }
    pthread_mutex_unlock(&cache_lock);
    pthread_mutex_destroy(&cache_lock);
}

cache_entry_t* find_in_cache(const char *path) {
    time_t now = time(NULL);
    pthread_mutex_lock(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].path, path) == 0) {
            cache[i].last_accessed = now;
            pthread_mutex_unlock(&cache_lock);
            return &cache[i];
        }
    }
    pthread_mutex_unlock(&cache_lock);
    return NULL;
}

void add_to_cache(const char *path, const char *content, size_t size) {
    time_t now = time(NULL);
    pthread_mutex_lock(&cache_lock);

    if (cache_count < MAX_CACHE_SIZE) {
        strncpy(cache[cache_count].path, path, sizeof(cache[cache_count].path) - 1);
        cache[cache_count].path[sizeof(cache[cache_count].path) - 1] = '\0';
        cache[cache_count].content = malloc(size);
        if (cache[cache_count].content != NULL) {
            memcpy(cache[cache_count].content, content, size);
            cache[cache_count].size = size;
            cache[cache_count].last_accessed = now;
            cache_count++;
        } else {
            // 处理内存分配失败的情况
            log_message("Failed to allocate memory for cache content");
        }

    } else {
        int oldest_index = 0;
        for (int i = 1; i < cache_count; i++) {
            if (cache[i].last_accessed < cache[oldest_index].last_accessed) {
                oldest_index = i;
            }
        }
        free(cache[oldest_index].content);
        strncpy(cache[oldest_index].path, path, sizeof(cache[oldest_index].path) - 1);
        cache[oldest_index].path[sizeof(cache[oldest_index].path) - 1] = '\0';
        cache[oldest_index].content = malloc(size);
        if (cache[oldest_index].content != NULL) {
            memcpy(cache[oldest_index].content, content, size);
            cache[oldest_index].size = size;
            cache[oldest_index].last_accessed = now;
        } else {
            // 处理内存分配失败的情况
            log_message("Failed to allocate memory for cache content");
        }
    }

    pthread_mutex_unlock(&cache_lock);
}

void evict_expired_cache() {
    time_t now = time(NULL);
    pthread_mutex_lock(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        if (difftime(now, cache[i].last_accessed) > CACHE_TTL) {
            free(cache[i].content);
            cache[i] = cache[cache_count - 1];
            cache_count--;
            i--;
        }
    }
    pthread_mutex_unlock(&cache_lock);
}
