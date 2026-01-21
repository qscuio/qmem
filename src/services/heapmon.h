/*
 * heapmon.h - Heap monitoring via /proc/pid/smaps
 */
#ifndef QMEM_HEAPMON_H
#define QMEM_HEAPMON_H

#include "service.h"
#include <sys/types.h>

extern qmem_service_t heapmon_service;

/* Heap info for a process */
typedef struct {
    pid_t pid;
    char cmd[128];
    int64_t heap_size_kb;
    int64_t heap_rss_kb;
    int64_t heap_private_dirty_kb;
    int64_t rss_kb;                 /* From procmem/status */
    int64_t rss_delta_kb;           /* From procmem */
    int64_t heap_rss_delta_kb;
    int64_t heap_pd_delta_kb;
} heapmon_entry_t;

/* Set top RSS growers to scan (called by procmem) */
void heapmon_set_targets(pid_t *pids, int count);

/* Get heap info for scanned processes */
int heapmon_get_entries(heapmon_entry_t *entries, int max_entries);

#endif /* QMEM_HEAPMON_H */
