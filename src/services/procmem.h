/*
 * procmem.h - Per-process memory monitor
 */
#ifndef QMEM_PROCMEM_H
#define QMEM_PROCMEM_H

#include "service.h"
#include <sys/types.h>

extern qmem_service_t procmem_service;

/* Process memory entry */
typedef struct {
    pid_t pid;
    char cmd[128];
    int64_t rss_kb;
    int64_t data_kb;
    int64_t rss_delta_kb;
    int64_t data_delta_kb;
} procmem_entry_t;

/* Get top N RSS growers/shrinkers */
int procmem_get_top_growers(procmem_entry_t *entries, int max_entries);
int procmem_get_top_shrinkers(procmem_entry_t *entries, int max_entries);

#endif /* QMEM_PROCMEM_H */
