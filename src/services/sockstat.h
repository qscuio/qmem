/*
 * sockstat.h - Socket statistics monitor
 */
#ifndef QMEM_SOCKSTAT_H
#define QMEM_SOCKSTAT_H

#include "service.h"

extern qmem_service_t sockstat_service;

/* Socket state codes */
typedef enum {
    SOCK_ESTABLISHED = 1,
    SOCK_SYN_SENT,
    SOCK_SYN_RECV,
    SOCK_FIN_WAIT1,
    SOCK_FIN_WAIT2,
    SOCK_TIME_WAIT,
    SOCK_CLOSE,
    SOCK_CLOSE_WAIT,
    SOCK_LAST_ACK,
    SOCK_LISTEN,
    SOCK_CLOSING,
} sock_state_t;

/* Socket summary by state */
typedef struct {
    int tcp_established;
    int tcp_time_wait;
    int tcp_close_wait;
    int tcp_listen;
    int tcp_total;
    int udp_total;
    int unix_total;
} sockstat_summary_t;

/* Get socket summary */
const sockstat_summary_t *sockstat_get_summary(void);

#endif /* QMEM_SOCKSTAT_H */
