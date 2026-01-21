/*
 * sockstat.c - Socket statistics monitor
 * 
 * Reads /proc/net/tcp, /proc/net/udp, /proc/net/unix
 */
#include "sockstat.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    sockstat_summary_t summary;
} sockstat_priv_t;

static sockstat_priv_t g_sockstat;

static int sockstat_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    memset(&g_sockstat, 0, sizeof(g_sockstat));
    svc->priv = &g_sockstat;
    log_debug("sockstat service initialized");
    return 0;
}

static int count_lines(const char *path, int skip_header) {
    char buf[65536];
    if (proc_read_file(path, buf, sizeof(buf)) < 0) {
        return 0;
    }
    
    int count = 0;
    int skipped = 0;
    char *line = buf;
    
    while (*line) {
        if (skipped >= skip_header) {
            count++;
        } else {
            skipped++;
        }
        /* Move to next line */
        char *nl = strchr(line, '\n');
        if (!nl) break;
        line = nl + 1;
    }
    
    return count;
}

static int parse_tcp_states(const char *path, sockstat_summary_t *sum) {
    char buf[65536];
    if (proc_read_file(path, buf, sizeof(buf)) < 0) {
        return -1;
    }
    
    /* Format: sl local_address rem_address st tx_queue rx_queue ... */
    char *line = strchr(buf, '\n');  /* Skip header */
    if (!line) return -1;
    line++;
    
    while (*line) {
        /* Extract state (field 3, hex) */
        unsigned int sl, state;
        char local_addr[64], rem_addr[64];
        if (sscanf(line, "%u: %63s %63s %02X", &sl, local_addr, rem_addr, &state) == 4) {
            sum->tcp_total++;
            switch (state) {
                case SOCK_ESTABLISHED: sum->tcp_established++; break;
                case SOCK_TIME_WAIT: sum->tcp_time_wait++; break;
                case SOCK_CLOSE_WAIT: sum->tcp_close_wait++; break;
                case SOCK_LISTEN: sum->tcp_listen++; break;
            }
        }
        
        char *nl = strchr(line, '\n');
        if (!nl) break;
        line = nl + 1;
    }
    
    return 0;
}

static int sockstat_collect(qmem_service_t *svc) {
    sockstat_priv_t *priv = (sockstat_priv_t *)svc->priv;
    
    memset(&priv->summary, 0, sizeof(priv->summary));
    
    /* Parse TCP sockets */
    parse_tcp_states("/proc/net/tcp", &priv->summary);
    parse_tcp_states("/proc/net/tcp6", &priv->summary);
    
    /* Count UDP sockets */
    priv->summary.udp_total = count_lines("/proc/net/udp", 1) + 
                              count_lines("/proc/net/udp6", 1);
    
    /* Count Unix sockets */
    priv->summary.unix_total = count_lines("/proc/net/unix", 1);
    
    return 0;
}

static int sockstat_snapshot(qmem_service_t *svc, json_builder_t *j) {
    sockstat_priv_t *priv = (sockstat_priv_t *)svc->priv;
    
    json_object_start(j);
    
    json_key(j, "tcp");
    json_object_start(j);
    json_kv_int(j, "total", priv->summary.tcp_total);
    json_kv_int(j, "established", priv->summary.tcp_established);
    json_kv_int(j, "time_wait", priv->summary.tcp_time_wait);
    json_kv_int(j, "close_wait", priv->summary.tcp_close_wait);
    json_kv_int(j, "listen", priv->summary.tcp_listen);
    json_object_end(j);
    
    json_kv_int(j, "udp_total", priv->summary.udp_total);
    json_kv_int(j, "unix_total", priv->summary.unix_total);
    
    json_object_end(j);
    return 0;
}

static void sockstat_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("sockstat service destroyed");
}

static const qmem_service_ops_t sockstat_ops = {
    .init = sockstat_init,
    .collect = sockstat_collect,
    .snapshot = sockstat_snapshot,
    .destroy = sockstat_destroy,
};

qmem_service_t sockstat_service = {
    .name = "sockstat",
    .description = "Socket statistics from /proc/net/tcp,udp,unix",
    .ops = &sockstat_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

const sockstat_summary_t *sockstat_get_summary(void) {
    return &g_sockstat.summary;
}
