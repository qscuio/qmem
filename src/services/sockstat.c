/*
 * sockstat.c - Socket statistics monitor
 */
#define _POSIX_C_SOURCE 200809L
#include "sockstat.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <qmem/plugin.h>

#define MAX_SOCKETS 1024

typedef struct {
    sockstat_summary_t summary;
    sockstat_summary_t previous_summary;
    socket_entry_t sockets[MAX_SOCKETS];
    int socket_count;
    bool has_previous;
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
    if (proc_read_file(path, buf, sizeof(buf)) < 0) return 0;
    
    int count = 0;
    int skipped = 0;
    char *line = buf;
    
    while (*line) {
        if (skipped >= skip_header) {
            count++;
        } else {
            skipped++;
        }
        char *nl = strchr(line, '\n');
        if (!nl) break;
        line = nl + 1;
    }
    return count;
}

static void parse_address(const char *hex_addr, char *out_buf, size_t size) {
    unsigned int addr, port;
    if (sscanf(hex_addr, "%X:%X", &addr, &port) == 2) {
        struct in_addr in;
        in.s_addr = addr;
        snprintf(out_buf, size, "%s:%u", inet_ntoa(in), port);
    } else {
        /* Try IPv6? For now just copy raw */
        strncpy(out_buf, hex_addr, size - 1);
        out_buf[size - 1] = '\0';
    }
}

static void map_inodes_to_pids(sockstat_priv_t *priv) {
    DIR *proc = opendir("/proc");
    if (!proc) return;
    
    struct dirent *ent;
    while ((ent = readdir(proc)) != NULL) {
        if (!isdigit(ent->d_name[0])) continue;
        
        pid_t pid = atoi(ent->d_name);
        char fd_path[64];
        snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", pid);
        
        DIR *fd_dir = opendir(fd_path);
        if (!fd_dir) continue;
        
        struct dirent *fd_ent;
        while ((fd_ent = readdir(fd_dir)) != NULL) {
            if (fd_ent->d_name[0] == '.') continue;
            
            char link_path[512];
            snprintf(link_path, sizeof(link_path), "%s/%s", fd_path, fd_ent->d_name);
            
            char target[128];
            ssize_t len = readlink(link_path, target, sizeof(target) - 1);
            if (len > 0) {
                target[len] = '\0';
                if (strncmp(target, "socket:[", 8) == 0) {
                    uint32_t inode = (uint32_t)strtoul(target + 8, NULL, 10);
                    
                    /* Check if this inode is in our list */
                    for (int i = 0; i < priv->socket_count; i++) {
                        if (priv->sockets[i].inode == inode) {
                            priv->sockets[i].pid = pid;
                            
                            /* Get command name */
                            char cmd_path[64];
                            snprintf(cmd_path, sizeof(cmd_path), "/proc/%d/comm", pid);
                            char cmd[16];
                            if (proc_read_file(cmd_path, cmd, sizeof(cmd)) > 0) {
                                char *nl = strchr(cmd, '\n');
                                if (nl) *nl = '\0';
                                snprintf(priv->sockets[i].cmd, sizeof(priv->sockets[i].cmd), "%s", cmd);
                            }
                        }
                    }
                }
            }
        }
        closedir(fd_dir);
    }
    closedir(proc);
}

static int parse_tcp_detailed(const char *path, sockstat_priv_t *priv) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[512];
    /* Skip header */
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    
    while (fgets(line, sizeof(line), f) && priv->socket_count < MAX_SOCKETS) {
        unsigned int sl, state, tx_q, rx_q, timer_active, timer_len, uid, timeout, inode;
        char local_addr_hex[64], rem_addr_hex[64];
        unsigned long retrans;
        
        /* 
         * Format: 
         *   sl  local_address rem_address   st tx_queue:rx_queue tr:tm->when retrnsmt   uid  timeout inode
         */
         
        if (sscanf(line, "%u: %63s %63s %X %X:%X %X:%X %lX %u %u %u",
                   &sl, local_addr_hex, rem_addr_hex, &state, 
                   &tx_q, &rx_q, &timer_active, &timer_len, &retrans, &uid, &timeout, &inode) >= 12) {
            
            /* Update summary */
            priv->summary.tcp_total++;
            switch (state) {
                case SOCK_ESTABLISHED: priv->summary.tcp_established++; break;
                case SOCK_TIME_WAIT: priv->summary.tcp_time_wait++; break;
                case SOCK_CLOSE_WAIT: priv->summary.tcp_close_wait++; break;
                case SOCK_LISTEN: priv->summary.tcp_listen++; break;
            }
            
            /* Store detailed info */
            socket_entry_t *s = &priv->sockets[priv->socket_count++];
            parse_address(local_addr_hex, s->local_addr, sizeof(s->local_addr));
            parse_address(rem_addr_hex, s->rem_addr, sizeof(s->rem_addr));
            s->state = state;
            s->tx_queue = tx_q;
            s->rx_queue = rx_q;
            s->inode = inode;
            s->pid = 0;
            s->cmd[0] = '\0';
        }
    }
    
    fclose(f);
    return 0;
}

static int sockstat_collect(qmem_service_t *svc) {
    sockstat_priv_t *priv = (sockstat_priv_t *)svc->priv;
    
    /* Save previous */
    priv->previous_summary = priv->summary;
    priv->has_previous = true;
    
    memset(&priv->summary, 0, sizeof(priv->summary));
    priv->socket_count = 0;
    
    /* Parse TCP sockets */
    parse_tcp_detailed("/proc/net/tcp", priv);
    
    /* Map IDs */
    map_inodes_to_pids(priv);
    
    /* Count others */
    priv->summary.udp_total = count_lines("/proc/net/udp", 1) + 
                              count_lines("/proc/net/udp6", 1);
    
    priv->summary.unix_total = count_lines("/proc/net/unix", 1);
    
    return 0;
}

static int sockstat_snapshot(qmem_service_t *svc, json_builder_t *j) {
    sockstat_priv_t *priv = (sockstat_priv_t *)svc->priv;
    
    json_object_start(j);
    
    json_key(j, "tcp");
    json_object_start(j);
    json_kv_int(j, "total", priv->summary.tcp_total);
    json_kv_int(j, "total_delta", priv->summary.tcp_total - priv->previous_summary.tcp_total);
    json_kv_int(j, "established", priv->summary.tcp_established);
    json_kv_int(j, "established_delta", priv->summary.tcp_established - priv->previous_summary.tcp_established);
    json_kv_int(j, "time_wait", priv->summary.tcp_time_wait);
    json_kv_int(j, "time_wait_delta", priv->summary.tcp_time_wait - priv->previous_summary.tcp_time_wait);
    json_kv_int(j, "close_wait", priv->summary.tcp_close_wait);
    json_kv_int(j, "listen", priv->summary.tcp_listen);
    json_object_end(j);
    
    json_kv_int(j, "udp_total", priv->summary.udp_total);
    json_kv_int(j, "udp_total_delta", priv->summary.udp_total - priv->previous_summary.udp_total);
    json_kv_int(j, "unix_total", priv->summary.unix_total);
    json_kv_int(j, "unix_total_delta", priv->summary.unix_total - priv->previous_summary.unix_total);
    
    /* Add detailed sockets list */
    json_key(j, "sockets");
    json_array_start(j);
    for (int i = 0; i < priv->socket_count; i++) {
        socket_entry_t *s = &priv->sockets[i];
        json_object_start(j);
        json_kv_string(j, "local", s->local_addr);
        json_kv_string(j, "remote", s->rem_addr);
        json_kv_int(j, "state", s->state);
        json_kv_uint(j, "tx_q", s->tx_queue);
        json_kv_uint(j, "rx_q", s->rx_queue);
        json_kv_uint(j, "inode", s->inode);
        if (s->pid > 0) {
            json_kv_int(j, "pid", s->pid);
            json_kv_string(j, "cmd", s->cmd);
        }
        json_object_end(j);
    }
    json_array_end(j);
    
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

QMEM_PLUGIN_DEFINE("sockstat", "1.0", "Socket statistics", sockstat_service);

const sockstat_summary_t *sockstat_get_summary(void) {
    return &g_sockstat.summary;
}

int sockstat_get_sockets(socket_entry_t *sockets, int max_sockets) {
    int n = g_sockstat.socket_count;
    if (n > max_sockets) n = max_sockets;
    memcpy(sockets, g_sockstat.sockets, n * sizeof(socket_entry_t));
    return n;
}
