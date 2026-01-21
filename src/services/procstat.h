/*
 * procstat.h - Process status monitor (active/blocked state)
 */
#ifndef QMEM_PROCSTAT_H
#define QMEM_PROCSTAT_H

#include "service.h"
#include <sys/types.h>

extern qmem_service_t procstat_service;

/* Process state codes (from /proc/pid/stat) */
typedef enum {
    PROC_STATE_RUNNING = 'R',      /* Running */
    PROC_STATE_SLEEPING = 'S',     /* Sleeping (interruptible) */
    PROC_STATE_DISK_SLEEP = 'D',   /* Disk sleep (uninterruptible) */
    PROC_STATE_ZOMBIE = 'Z',       /* Zombie */
    PROC_STATE_STOPPED = 'T',      /* Stopped */
    PROC_STATE_TRACING = 't',      /* Tracing stop */
    PROC_STATE_DEAD = 'X',         /* Dead */
    PROC_STATE_IDLE = 'I',         /* Idle kernel thread */
} proc_state_t;

/* Process/thread status entry */
typedef struct {
    pid_t pid;
    pid_t tid;                     /* Thread ID (same as pid for main thread) */
    char cmd[128];
    char state;                    /* State character */
    const char *state_desc;        /* Human-readable state description */
    char wchan[64];                /* Wait channel (blocked syscall) */
    unsigned long blocked_time;    /* Approx. time in D state (if applicable) */
    bool is_blocked;               /* In D (disk sleep) state */
} procstat_entry_t;

/* State summary */
typedef struct {
    int running;
    int sleeping;
    int disk_sleep;               /* Blocked on I/O */
    int zombie;
    int stopped;
    int total;
} procstat_summary_t;

/* Get summary of all process states */
const procstat_summary_t *procstat_get_summary(void);

/* Get blocked processes (D state) */
int procstat_get_blocked(procstat_entry_t *entries, int max_entries);

/* Get all process/thread states for a specific PID */
int procstat_get_threads(pid_t pid, procstat_entry_t *entries, int max_entries);

#endif /* QMEM_PROCSTAT_H */
