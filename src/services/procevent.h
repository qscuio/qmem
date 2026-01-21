/*
 * procevent.h - Process create/destroy event monitor
 */
#ifndef QMEM_PROCEVENT_H
#define QMEM_PROCEVENT_H

#include "service.h"
#include <sys/types.h>

extern qmem_service_t procevent_service;

/* Event types */
typedef enum {
    PROC_EVENT_FORK = 1,
    PROC_EVENT_EXEC,
    PROC_EVENT_EXIT,
} proc_event_type_t;

/* Process event */
typedef struct {
    proc_event_type_t type;
    pid_t pid;
    pid_t parent_pid;
    int exit_code;                 /* For exit events */
    char cmd[128];
    uint64_t timestamp;
} proc_event_t;

/* Event counters */
typedef struct {
    uint64_t forks;
    uint64_t execs;
    uint64_t exits;
} proc_event_counters_t;

/* Get event counters */
const proc_event_counters_t *procevent_get_counters(void);

/* Get recent events */
int procevent_get_recent(proc_event_t *events, int max_events);

#endif /* QMEM_PROCEVENT_H */
