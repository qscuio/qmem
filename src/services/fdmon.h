/*
 * fdmon.h - File descriptor monitoring
 * Tracks per-process FD counts and detects potential FD leaks
 */
#ifndef QMEM_FDMON_H
#define QMEM_FDMON_H

#include "service.h"
#include <sys/types.h>

extern qmem_service_t fdmon_service;

/* FD types */
typedef struct {
    int files;      /* Regular files */
    int sockets;    /* Network sockets */
    int pipes;      /* Pipes/FIFOs */
    int eventfds;   /* eventfd, signalfd, timerfd, etc. */
    int other;      /* Other types */
} fdmon_fd_types_t;

/* Per-process FD entry */
typedef struct {
    pid_t pid;
    char cmd[128];
    int fd_count;           /* Current FD count */
    int initial_fd_count;   /* Baseline FD count (first seen) */
    int fd_delta;           /* Change since last sample */
    int fd_change;          /* Total change since initial */
    fdmon_fd_types_t types; /* Breakdown by type */
} fdmon_entry_t;

/* System-wide FD summary */
typedef struct {
    int total_fds;          /* Total FDs across all processes */
    int total_delta;        /* Change since last sample */
    int proc_count;         /* Number of processes tracked */
    int potential_leaks;    /* Processes with growing FD count */
} fdmon_summary_t;

/* Get top FD consumers */
int fdmon_get_top_consumers(fdmon_entry_t *entries, int max_entries);

/* Get potential FD leakers (processes with growing FD count) */
int fdmon_get_leakers(fdmon_entry_t *entries, int max_entries);

/* Get summary */
const fdmon_summary_t *fdmon_get_summary(void);

#endif /* QMEM_FDMON_H */
