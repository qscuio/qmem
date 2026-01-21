/*
 * sockstat.h - Socket statistics monitor
 */
#ifndef QMEM_SOCKSTAT_H
#define QMEM_SOCKSTAT_H

#include "service.h"
#include <sys/types.h>

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

typedef struct {
    char local_addr[64];
    char rem_addr[64];
    uint32_t state;
    uint32_t tx_queue;
    uint32_t rx_queue;
    uint32_t inode;
    pid_t pid;
    char cmd[16];
} socket_entry_t;

/* Get socket summary */
const sockstat_summary_t *sockstat_get_summary(void);

/* Get active sockets */
int sockstat_get_sockets(socket_entry_t *sockets, int max_sockets);

#endif /* QMEM_SOCKSTAT_H */
