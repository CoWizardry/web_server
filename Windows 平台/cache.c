#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static cache_entry_t cache[MAX_CACHE_SIZE];
static int cache_count = 0;
static CRITICAL_SECTION cache_lock;

void init_cache() {
    InitializeCriticalSection(&cache_lock);
    cache_count = 0;
}

void cleanup_cache() {
    EnterCriticalSection(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        free(cache[i].content);
    }
    LeaveCriticalSection(&cache_lock);
    DeleteCriticalSection(&cache_lock);
}

cache_entry_t* find_in_cache(const wchar_t *path) {
    time_t now = time(NULL);
    EnterCriticalSection(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        if (wcscmp(cache[i].path, path) == 0) {
            cache[i].last_accessed = now;
            LeaveCriticalSection(&cache_lock);
            return &cache[i];
        }
    }
    LeaveCriticalSection(&cache_lock);
    return NULL;
}

void add_to_cache(const wchar_t *path, const char *content, size_t size) {
    EnterCriticalSection(&cache_lock);
    if (cache_count < MAX_CACHE_SIZE) {
        wcscpy(cache[cache_count].path, path);
        cache[cache_count].content = malloc(size);
        memcpy(cache[cache_count].content, content, size);
        cache[cache_count].size = size;
        cache[cache_count].last_accessed = time(NULL);
        cache_count++;
    } else {
        // Cache is full, evict the oldest entry
        int oldest = 0;
        for (int i = 1; i < cache_count; i++) {
            if (cache[i].last_accessed < cache[oldest].last_accessed) {
                oldest = i;
            }
        }
        free(cache[oldest].content);
        wcscpy(cache[oldest].path, path);
        cache[oldest].content = malloc(size);
        memcpy(cache[oldest].content, content, size);
        cache[oldest].size = size;
        cache[oldest].last_accessed = time(NULL);
    }
    LeaveCriticalSection(&cache_lock);
}

void evict_expired_cache() {
    time_t now = time(NULL);
    EnterCriticalSection(&cache_lock);
    for (int i = 0; i < cache_count; ) {
        if (difftime(now, cache[i].last_accessed) > CACHE_TTL) {
            free(cache[i].content);
            cache[i] = cache[--cache_count];
        } else {
            i++;
        }
    }
    LeaveCriticalSection(&cache_lock);
}
