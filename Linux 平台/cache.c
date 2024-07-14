#include "cache.h"
#include "log.h"  // 添加日志模块头文件
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static cache_entry_t cache[MAX_CACHE_SIZE];
static int cache_count = 0;
static pthread_mutex_t cache_lock;

void init_cache() {
    pthread_mutex_init(&cache_lock, NULL);
    cache_count = 0;
    log_message(INFO, "Cache initialized");
}

void cleanup_cache() {
    pthread_mutex_lock(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        free(cache[i].content);
    }
    cache_count = 0;
    pthread_mutex_unlock(&cache_lock);
    pthread_mutex_destroy(&cache_lock);
    log_message(INFO, "Cache cleaned up");
}

cache_entry_t* find_in_cache(const char *path) {
    time_t now = time(NULL);
    pthread_mutex_lock(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].path, path) == 0) {
            cache[i].last_accessed = now;
            pthread_mutex_unlock(&cache_lock);
            log_message(INFO, "Cache hit for path: %s", path);
            return &cache[i];
        }
    }
    pthread_mutex_unlock(&cache_lock);
    log_message(INFO, "Cache miss for path: %s", path);
    return NULL;
}

void add_to_cache(const char *path, const char *content, size_t size) {
    time_t now = time(NULL);
    pthread_mutex_lock(&cache_lock);

    if (cache_count < MAX_CACHE_SIZE) {
        strncpy(cache[cache_count].path, path, sizeof(cache[cache_count].path) - 1);
        cache[cache_count].path[sizeof(cache[cache_count].path) - 1] = '\0';
        cache[cache_count].content = malloc(size);
        memcpy(cache[cache_count].content, content, size);
        cache[cache_count].size = size;
        cache[cache_count].last_accessed = now;
        cache_count++;
        log_message(INFO, "Added to cache: %s", path);
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
        memcpy(cache[oldest_index].content, content, size);
        cache[oldest_index].size = size;
        cache[oldest_index].last_accessed = now;
        log_message(INFO, "Evicted oldest entry and added to cache: %s", path);
    }

    pthread_mutex_unlock(&cache_lock);
}

void evict_expired_cache() {
    time_t now = time(NULL);
    pthread_mutex_lock(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        if (difftime(now, cache[i].last_accessed) > CACHE_TTL) {
            log_message(INFO, "Evicted expired cache entry: %s", cache[i].path);
            free(cache[i].content);
            cache[i] = cache[cache_count - 1];
            cache_count--;
            i--;
        }
    }
    pthread_mutex_unlock(&cache_lock);
}
