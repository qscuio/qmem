/*
 * netstat.h - Network interface statistics monitor
 */
#ifndef QMEM_NETSTAT_H
#define QMEM_NETSTAT_H

#include "service.h"

extern qmem_service_t netstat_service;

/* Network interface statistics */
typedef struct {
    char name[32];             /* Interface name (eth0, lo, etc.) */
    
    /* Current values */
    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t rx_errors;
    uint64_t rx_dropped;
    uint64_t tx_bytes;
    uint64_t tx_packets;
    uint64_t tx_errors;
    uint64_t tx_dropped;
    
    /* Deltas since last sample */
    int64_t rx_bytes_delta;
    int64_t rx_packets_delta;
    int64_t tx_bytes_delta;
    int64_t tx_packets_delta;
    
    /* Rates (bytes/sec, packets/sec) */
    double rx_rate;
    double tx_rate;
} netstat_iface_t;

/* Get all interfaces */
int netstat_get_interfaces(netstat_iface_t *interfaces, int max_interfaces);

/* Get interface by name */
const netstat_iface_t *netstat_get_interface(const char *name);

#endif /* QMEM_NETSTAT_H */
