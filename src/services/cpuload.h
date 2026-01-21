/*
 * cpuload.h - Per-process CPU load monitor
 */
#ifndef QMEM_CPULOAD_H
#define QMEM_CPULOAD_H

#include "service.h"
#include <sys/types.h>

extern qmem_service_t cpuload_service;

/* CPU load entry for a process */
typedef struct {
    pid_t pid;
    char cmd[128];
    double cpu_percent;        /* CPU usage percentage */
    double cpu_delta;          /* Change since last sample */
    unsigned long utime;       /* User time (jiffies) */
    unsigned long stime;       /* System time (jiffies) */
} cpuload_entry_t;

/* Get top N CPU consumers */
int cpuload_get_top(cpuload_entry_t *entries, int max_entries);

/* Get system-wide CPU stats */
typedef struct {
    double user_percent;
    double system_percent;
    double idle_percent;
    double iowait_percent;
} cpuload_system_t;

const cpuload_system_t *cpuload_get_system(void);

#endif /* QMEM_CPULOAD_H */
