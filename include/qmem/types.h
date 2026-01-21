/*
 * qmem/types.h - Common type definitions
 */
#ifndef QMEM_TYPES_H
#define QMEM_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* Memory sizes in kilobytes */
typedef int64_t mem_kb_t;

/* Process ID */
typedef int32_t pid_t;

/* Error codes */
typedef enum {
    QMEM_OK = 0,
    QMEM_ERR_NOMEM = -1,
    QMEM_ERR_IO = -2,
    QMEM_ERR_PARSE = -3,
    QMEM_ERR_CONFIG = -4,
    QMEM_ERR_SOCKET = -5,
    QMEM_ERR_PERM = -6,
    QMEM_ERR_NOTFOUND = -7,
    QMEM_ERR_BUSY = -8,
    QMEM_ERR_INVALID = -9,
} qmem_err_t;

/* Memory delta with direction */
typedef struct {
    mem_kb_t value;
    mem_kb_t delta;
    bool is_growing;
} mem_delta_t;

/* Process memory info */
typedef struct {
    int32_t pid;
    mem_kb_t rss_kb;
    mem_kb_t data_kb;
    mem_kb_t rss_delta;
    mem_kb_t data_delta;
    char cmd[128];
} proc_mem_t;

/* Heap info from smaps */
typedef struct {
    int32_t pid;
    mem_kb_t heap_size_kb;
    mem_kb_t heap_rss_kb;
    mem_kb_t heap_private_dirty_kb;
    mem_kb_t heap_rss_delta;
    mem_kb_t heap_pd_delta;
} heap_info_t;

/* Slab cache info */
typedef struct {
    char name[64];
    int64_t size_bytes;
    int64_t delta_bytes;
} slab_cache_t;

/* Timestamp */
typedef struct timespec qmem_time_t;

/* Get current time */
static inline void qmem_time_now(qmem_time_t *t) {
    clock_gettime(CLOCK_MONOTONIC, t);
}

/* Time difference in milliseconds */
static inline int64_t qmem_time_diff_ms(const qmem_time_t *start, const qmem_time_t *end) {
    return (end->tv_sec - start->tv_sec) * 1000 +
           (end->tv_nsec - start->tv_nsec) / 1000000;
}

#endif /* QMEM_TYPES_H */
