/*
 * heapmon.c - Heap monitoring via /proc/pid/smaps
 */
#include "heapmon.h"
#include "procmem.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TARGETS 12

typedef struct {
    pid_t pid;
    int64_t heap_size_kb;
    int64_t heap_rss_kb;
    int64_t heap_pd_kb;
} heap_data_t;

typedef struct {
    /* Targets from procmem (top RSS growers) */
    pid_t targets[MAX_TARGETS];
    int target_count;
    
    /* Current and previous heap data */
    heap_data_t current[MAX_TARGETS];
    heap_data_t previous[MAX_TARGETS];
    int current_count;
    int previous_count;
    bool has_previous;
    
    /* Results */
    heapmon_entry_t results[MAX_TARGETS];
    int result_count;
} heapmon_priv_t;

static heapmon_priv_t g_heapmon;

static int heapmon_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_heapmon, 0, sizeof(g_heapmon));
    svc->priv = &g_heapmon;
    
    log_debug("heapmon service initialized");
    return 0;
}

static int parse_heap_smaps(pid_t pid, int64_t *size_kb, int64_t *rss_kb, int64_t *pd_kb) {
    char path[64];
    char buf[65536];
    
    snprintf(path, sizeof(path), "/proc/%d/smaps", pid);
    
    if (proc_read_file(path, buf, sizeof(buf)) < 0) {
        return -1;
    }
    
    *size_kb = 0;
    *rss_kb = 0;
    *pd_kb = 0;
    
    bool in_heap = false;
    char *line = buf;
    
    while (*line) {
        /* Check for mapping line (starts with hex address) */
        if ((line[0] >= '0' && line[0] <= '9') ||
            (line[0] >= 'a' && line[0] <= 'f')) {
            /* Check if this is a [heap] mapping */
            in_heap = (strstr(line, "[heap]") != NULL);
        }
        
        if (in_heap) {
            /* Parse Size, Rss, Private_Dirty */
            if (strncmp(line, "Size:", 5) == 0) {
                *size_kb += strtoll(line + 5, NULL, 10);
            } else if (strncmp(line, "Rss:", 4) == 0) {
                *rss_kb += strtoll(line + 4, NULL, 10);
            } else if (strncmp(line, "Private_Dirty:", 14) == 0) {
                *pd_kb += strtoll(line + 14, NULL, 10);
            }
        }
        
        /* Next line */
        line = strchr(line, '\n');
        if (!line) break;
        line++;
    }
    
    return 0;
}

static heap_data_t *find_previous(heapmon_priv_t *priv, pid_t pid) {
    for (int i = 0; i < priv->previous_count; i++) {
        if (priv->previous[i].pid == pid) {
            return &priv->previous[i];
        }
    }
    return NULL;
}

static int heapmon_collect(qmem_service_t *svc) {
    heapmon_priv_t *priv = (heapmon_priv_t *)svc->priv;
    
    /* Save previous */
    memcpy(priv->previous, priv->current, sizeof(priv->previous));
    priv->previous_count = priv->current_count;
    
    /* If no targets set, get them from procmem */
    if (priv->target_count == 0) {
        procmem_entry_t growers[MAX_TARGETS];
        int n = procmem_get_top_growers(growers, MAX_TARGETS);
        priv->target_count = 0;
        for (int i = 0; i < n; i++) {
            priv->targets[priv->target_count++] = growers[i].pid;
        }
    }
    
    /* Scan heap for each target */
    priv->current_count = 0;
    priv->result_count = 0;
    
    for (int i = 0; i < priv->target_count; i++) {
        pid_t pid = priv->targets[i];
        heap_data_t *cur = &priv->current[priv->current_count];
        
        int64_t size_kb, rss_kb, pd_kb;
        if (parse_heap_smaps(pid, &size_kb, &rss_kb, &pd_kb) < 0) {
            continue;  /* Process may have exited */
        }
        
        cur->pid = pid;
        cur->heap_size_kb = size_kb;
        cur->heap_rss_kb = rss_kb;
        cur->heap_pd_kb = pd_kb;
        priv->current_count++;
        
        /* Build result entry */
        heapmon_entry_t *res = &priv->results[priv->result_count++];
        res->pid = pid;
        res->heap_size_kb = size_kb;
        res->heap_rss_kb = rss_kb;
        res->heap_private_dirty_kb = pd_kb;
        
        /* Get command name */
        if (proc_read_cmdline(pid, res->cmd, sizeof(res->cmd)) < 0) {
            proc_read_comm(pid, res->cmd, sizeof(res->cmd));
        }
        
        /* Calculate deltas */
        heap_data_t *prev = find_previous(priv, pid);
        if (prev) {
            res->heap_rss_delta_kb = rss_kb - prev->heap_rss_kb;
            res->heap_pd_delta_kb = pd_kb - prev->heap_pd_kb;
        } else {
            res->heap_rss_delta_kb = 0;
            res->heap_pd_delta_kb = 0;
        }
        
        /* Get RSS delta from procmem growers list */
        res->rss_delta_kb = 0;
        procmem_entry_t growers[MAX_TARGETS];
        int n = procmem_get_top_growers(growers, MAX_TARGETS);
        for (int j = 0; j < n; j++) {
            if (growers[j].pid == pid) {
                res->rss_delta_kb = growers[j].rss_delta_kb;
                break;
            }
        }
    }
    
    /* Clear targets for next round */
    priv->target_count = 0;
    priv->has_previous = true;
    
    return 0;
}

static int heapmon_snapshot(qmem_service_t *svc, json_builder_t *j) {
    heapmon_priv_t *priv = (heapmon_priv_t *)svc->priv;
    
    json_object_start(j);
    
    json_key(j, "heap_entries");
    json_array_start(j);
    
    for (int i = 0; i < priv->result_count; i++) {
        heapmon_entry_t *e = &priv->results[i];
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_string(j, "cmd", e->cmd);
        json_kv_int(j, "rss_delta_kb", e->rss_delta_kb);
        json_kv_int(j, "heap_size_kb", e->heap_size_kb);
        json_kv_int(j, "heap_rss_kb", e->heap_rss_kb);
        json_kv_int(j, "heap_pd_kb", e->heap_private_dirty_kb);
        json_kv_int(j, "heap_rss_delta_kb", e->heap_rss_delta_kb);
        json_kv_int(j, "heap_pd_delta_kb", e->heap_pd_delta_kb);
        json_object_end(j);
    }
    
    json_array_end(j);
    json_object_end(j);
    
    return 0;
}

static void heapmon_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("heapmon service destroyed");
}

static const qmem_service_ops_t heapmon_ops = {
    .init = heapmon_init,
    .collect = heapmon_collect,
    .snapshot = heapmon_snapshot,
    .destroy = heapmon_destroy,
};

qmem_service_t heapmon_service = {
    .name = "heapmon",
    .description = "Heap analysis via /proc/pid/smaps",
    .ops = &heapmon_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

void heapmon_set_targets(pid_t *pids, int count) {
    if (count > MAX_TARGETS) count = MAX_TARGETS;
    g_heapmon.target_count = count;
    memcpy(g_heapmon.targets, pids, count * sizeof(pid_t));
}

int heapmon_get_entries(heapmon_entry_t *entries, int max_entries) {
    int n = g_heapmon.result_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_heapmon.results, n * sizeof(heapmon_entry_t));
    return n;
}
