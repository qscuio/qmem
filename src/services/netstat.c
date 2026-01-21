/*
 * netstat.c - Network interface statistics monitor
 * 
 * Reads /proc/net/dev for interface statistics
 */
#include "netstat.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_INTERFACES 32

typedef struct {
    netstat_iface_t current[MAX_INTERFACES];
    netstat_iface_t previous[MAX_INTERFACES];
    int current_count;
    int previous_count;
    bool has_previous;
    time_t last_collect;
} netstat_priv_t;

static netstat_priv_t g_netstat;

static int netstat_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_netstat, 0, sizeof(g_netstat));
    svc->priv = &g_netstat;
    
    log_debug("netstat service initialized");
    return 0;
}

static int parse_net_dev(netstat_iface_t *interfaces, int *count) {
    char buf[8192];
    
    if (proc_read_file("/proc/net/dev", buf, sizeof(buf)) < 0) {
        log_error("Failed to read /proc/net/dev");
        return -1;
    }
    
    *count = 0;
    char *line = buf;
    int line_num = 0;
    
    while (*line && *count < MAX_INTERFACES) {
        /* Skip header lines */
        if (line_num < 2) {
            line = strchr(line, '\n');
            if (!line) break;
            line++;
            line_num++;
            continue;
        }
        
        /* Parse: iface: rx_bytes rx_packets rx_errs rx_drop ... tx_bytes tx_packets ... */
        netstat_iface_t *iface = &interfaces[*count];
        memset(iface, 0, sizeof(*iface));
        
        /* Find interface name */
        char *colon = strchr(line, ':');
        if (!colon) {
            line = strchr(line, '\n');
            if (!line) break;
            line++;
            line_num++;
            continue;
        }
        
        /* Extract name (trim whitespace) */
        char *name_start = line;
        while (*name_start == ' ') name_start++;
        size_t name_len = colon - name_start;
        if (name_len >= sizeof(iface->name)) name_len = sizeof(iface->name) - 1;
        memcpy(iface->name, name_start, name_len);
        iface->name[name_len] = '\0';
        
        /* Parse fields after colon */
        char *p = colon + 1;
        uint64_t rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        uint64_t tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;
        
        if (sscanf(p, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   &rx_bytes, &rx_packets, &rx_errs, &rx_drop, &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
                   &tx_bytes, &tx_packets, &tx_errs, &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed) == 16) {
            iface->rx_bytes = rx_bytes;
            iface->rx_packets = rx_packets;
            iface->rx_errors = rx_errs;
            iface->rx_dropped = rx_drop;
            iface->tx_bytes = tx_bytes;
            iface->tx_packets = tx_packets;
            iface->tx_errors = tx_errs;
            iface->tx_dropped = tx_drop;
            (*count)++;
        }
        
        line = strchr(line, '\n');
        if (!line) break;
        line++;
        line_num++;
    }
    
    return 0;
}

static netstat_iface_t *find_previous(netstat_priv_t *priv, const char *name) {
    for (int i = 0; i < priv->previous_count; i++) {
        if (strcmp(priv->previous[i].name, name) == 0) {
            return &priv->previous[i];
        }
    }
    return NULL;
}

static int netstat_collect(qmem_service_t *svc) {
    netstat_priv_t *priv = (netstat_priv_t *)svc->priv;
    
    /* Save previous */
    memcpy(priv->previous, priv->current, sizeof(priv->previous));
    priv->previous_count = priv->current_count;
    time_t prev_time = priv->last_collect;
    
    /* Read current */
    if (parse_net_dev(priv->current, &priv->current_count) < 0) {
        return -1;
    }
    
    priv->last_collect = time(NULL);
    double elapsed = (double)(priv->last_collect - prev_time);
    if (elapsed < 1) elapsed = 1;
    
    /* Calculate deltas and rates */
    if (priv->has_previous) {
        for (int i = 0; i < priv->current_count; i++) {
            netstat_iface_t *cur = &priv->current[i];
            netstat_iface_t *prev = find_previous(priv, cur->name);
            
            if (prev) {
                cur->rx_bytes_delta = cur->rx_bytes - prev->rx_bytes;
                cur->rx_packets_delta = cur->rx_packets - prev->rx_packets;
                cur->tx_bytes_delta = cur->tx_bytes - prev->tx_bytes;
                cur->tx_packets_delta = cur->tx_packets - prev->tx_packets;
                
                cur->rx_rate = (double)cur->rx_bytes_delta / elapsed;
                cur->tx_rate = (double)cur->tx_bytes_delta / elapsed;
            }
        }
    }
    
    priv->has_previous = true;
    return 0;
}

static int netstat_snapshot(qmem_service_t *svc, json_builder_t *j) {
    netstat_priv_t *priv = (netstat_priv_t *)svc->priv;
    
    json_object_start(j);
    
    json_key(j, "interfaces");
    json_array_start(j);
    
    for (int i = 0; i < priv->current_count; i++) {
        netstat_iface_t *iface = &priv->current[i];
        json_object_start(j);
        json_kv_string(j, "name", iface->name);
        
        json_key(j, "rx");
        json_object_start(j);
        json_kv_uint(j, "bytes", iface->rx_bytes);
        json_kv_uint(j, "packets", iface->rx_packets);
        json_kv_uint(j, "errors", iface->rx_errors);
        json_kv_uint(j, "dropped", iface->rx_dropped);
        json_kv_int(j, "bytes_delta", iface->rx_bytes_delta);
        json_kv_double(j, "rate", iface->rx_rate);
        json_object_end(j);
        
        json_key(j, "tx");
        json_object_start(j);
        json_kv_uint(j, "bytes", iface->tx_bytes);
        json_kv_uint(j, "packets", iface->tx_packets);
        json_kv_uint(j, "errors", iface->tx_errors);
        json_kv_uint(j, "dropped", iface->tx_dropped);
        json_kv_int(j, "bytes_delta", iface->tx_bytes_delta);
        json_kv_double(j, "rate", iface->tx_rate);
        json_object_end(j);
        
        json_object_end(j);
    }
    
    json_array_end(j);
    json_object_end(j);
    
    return 0;
}

static void netstat_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("netstat service destroyed");
}

static const qmem_service_ops_t netstat_ops = {
    .init = netstat_init,
    .collect = netstat_collect,
    .snapshot = netstat_snapshot,
    .destroy = netstat_destroy,
};

qmem_service_t netstat_service = {
    .name = "netstat",
    .description = "Network interface statistics from /proc/net/dev",
    .ops = &netstat_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

int netstat_get_interfaces(netstat_iface_t *interfaces, int max_interfaces) {
    int n = g_netstat.current_count;
    if (n > max_interfaces) n = max_interfaces;
    memcpy(interfaces, g_netstat.current, n * sizeof(netstat_iface_t));
    return n;
}

const netstat_iface_t *netstat_get_interface(const char *name) {
    for (int i = 0; i < g_netstat.current_count; i++) {
        if (strcmp(g_netstat.current[i].name, name) == 0) {
            return &g_netstat.current[i];
        }
    }
    return NULL;
}
