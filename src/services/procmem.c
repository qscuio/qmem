/*
 * procmem.c - Per-process memory monitor implementation
 */
#include "procmem.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qmem/plugin.h>

#define MAX_PROCS 4096
#define TOP_N 12
#define MIN_DELTA_KB 1024  /* 1 MB */

/* Hash table for tracking per-process memory */
#define HASH_SIZE 8192

typedef struct proc_entry {
    pid_t pid;
    int64_t rss_kb;
    int64_t data_kb;
    char cmd[128];
    struct proc_entry *next;
} proc_entry_t;

typedef struct {
    proc_entry_t *current[HASH_SIZE];
    proc_entry_t *previous[HASH_SIZE];
    bool has_previous;
    
    /* Sorted results */
    procmem_entry_t growers[TOP_N];
    procmem_entry_t shrinkers[TOP_N];
    procmem_entry_t top_rss[TOP_N];
    int grower_count;
    int shrinker_count;
    int top_rss_count;
    
    /* Memory pool for entries */
    proc_entry_t pool[MAX_PROCS * 2];
    int pool_idx;
} procmem_priv_t;

static procmem_priv_t g_procmem;

static unsigned int hash_pid(pid_t pid) {
    return (unsigned int)pid % HASH_SIZE;
}

static proc_entry_t *alloc_entry(procmem_priv_t *priv) {
    if (priv->pool_idx >= MAX_PROCS * 2) {
        return NULL;
    }
    return &priv->pool[priv->pool_idx++];
}

static void clear_hash(proc_entry_t **hash) {
    memset(hash, 0, sizeof(proc_entry_t *) * HASH_SIZE);
}

static proc_entry_t *find_in_hash(proc_entry_t **hash, pid_t pid) {
    unsigned int idx = hash_pid(pid);
    proc_entry_t *e = hash[idx];
    while (e) {
        if (e->pid == pid) return e;
        e = e->next;
    }
    return NULL;
}

static void insert_hash(proc_entry_t **hash, proc_entry_t *entry) {
    unsigned int idx = hash_pid(entry->pid);
    entry->next = hash[idx];
    hash[idx] = entry;
}

static int procmem_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_procmem, 0, sizeof(g_procmem));
    svc->priv = &g_procmem;
    
    log_debug("procmem service initialized");
    return 0;
}

typedef struct {
    procmem_priv_t *priv;
    procmem_entry_t *changes;
    int change_count;
    int max_changes;
} collect_ctx_t;

static bool collect_callback(pid_t pid, void *userdata) {
    collect_ctx_t *ctx = (collect_ctx_t *)userdata;
    procmem_priv_t *priv = ctx->priv;
    
    /* Read memory info */
    int64_t rss_kb = proc_read_status_kb(pid, "VmRSS");
    int64_t data_kb = proc_read_status_kb(pid, "VmData");
    
    if (rss_kb < 0 || data_kb < 0) {
        return true;  /* Process may have exited, continue */
    }
    
    /* Allocate and store current entry */
    proc_entry_t *entry = alloc_entry(priv);
    if (!entry) {
        return false;  /* Pool exhausted */
    }
    
    entry->pid = pid;
    entry->rss_kb = rss_kb;
    entry->data_kb = data_kb;
    entry->next = NULL;
    
    /* Get command */
    if (proc_read_cmdline(pid, entry->cmd, sizeof(entry->cmd)) < 0) {
        proc_read_comm(pid, entry->cmd, sizeof(entry->cmd));
    }
    
    insert_hash(priv->current, entry);
    
    /* Calculate delta if we have previous data */
    if (priv->has_previous) {
        proc_entry_t *prev = find_in_hash(priv->previous, pid);
        if (prev) {
            int64_t rss_delta = rss_kb - prev->rss_kb;
            int64_t data_delta = data_kb - prev->data_kb;
            int64_t abs_rss = rss_delta < 0 ? -rss_delta : rss_delta;
            int64_t abs_data = data_delta < 0 ? -data_delta : data_delta;
            
            if (abs_rss >= MIN_DELTA_KB || abs_data >= MIN_DELTA_KB) {
                if (ctx->change_count < ctx->max_changes) {
                    procmem_entry_t *e = &ctx->changes[ctx->change_count++];
                    e->pid = pid;
                    strncpy(e->cmd, entry->cmd, sizeof(e->cmd) - 1);
                    e->rss_kb = rss_kb;
                    e->data_kb = data_kb;
                    e->rss_delta_kb = rss_delta;
                    e->data_delta_kb = data_delta;
                }
            }
        }
    }
    
    return true;
}

static int compare_rss_grower(const void *a, const void *b) {
    const procmem_entry_t *ea = (const procmem_entry_t *)a;
    const procmem_entry_t *eb = (const procmem_entry_t *)b;
    if (eb->rss_delta_kb > ea->rss_delta_kb) return 1;
    if (eb->rss_delta_kb < ea->rss_delta_kb) return -1;
    return 0;
}

static int compare_rss_shrinker(const void *a, const void *b) {
    const procmem_entry_t *ea = (const procmem_entry_t *)a;
    const procmem_entry_t *eb = (const procmem_entry_t *)b;
    if (ea->rss_delta_kb < eb->rss_delta_kb) return -1;
    if (ea->rss_delta_kb > eb->rss_delta_kb) return 1;
    return 0;
}

static int compare_rss_abs(const void *a, const void *b) {
    const procmem_entry_t *ea = (const procmem_entry_t *)a;
    const procmem_entry_t *eb = (const procmem_entry_t *)b;
    if (eb->rss_kb > ea->rss_kb) return 1;
    if (eb->rss_kb < ea->rss_kb) return -1;
    return 0;
}

static int procmem_collect(qmem_service_t *svc) {
    procmem_priv_t *priv = (procmem_priv_t *)svc->priv;
    
    /* Swap current to previous */
    memcpy(priv->previous, priv->current, sizeof(priv->previous));
    clear_hash(priv->current);
    priv->pool_idx = 0;
    
    /* Collect all processes */
    procmem_entry_t all_changes[MAX_PROCS];
    collect_ctx_t ctx = {
        .priv = priv,
        .changes = all_changes,
        .change_count = 0,
        .max_changes = MAX_PROCS,
    };
    
    proc_iterate_pids(collect_callback, &ctx);
    
    /* Sort and take top growers */
    qsort(all_changes, ctx.change_count, sizeof(procmem_entry_t), compare_rss_grower);
    priv->grower_count = 0;
    for (int i = 0; i < ctx.change_count && priv->grower_count < TOP_N; i++) {
        if (all_changes[i].rss_delta_kb > 0) {
            priv->growers[priv->grower_count++] = all_changes[i];
        }
    }
    
    /* Sort and take top shrinkers */
    qsort(all_changes, ctx.change_count, sizeof(procmem_entry_t), compare_rss_shrinker);
    priv->shrinker_count = 0;
    for (int i = 0; i < ctx.change_count && priv->shrinker_count < TOP_N; i++) {
        if (all_changes[i].rss_delta_kb < 0) {
            priv->shrinkers[priv->shrinker_count++] = all_changes[i];
        }
    }
    
    /* Sort and take top absolute RSS (Using simple descending sort on rss_kb) */
    qsort(all_changes, ctx.change_count, sizeof(procmem_entry_t), compare_rss_abs);
    priv->top_rss_count = 0;
    for (int i = 0; i < ctx.change_count && priv->top_rss_count < TOP_N; i++) {
        priv->top_rss[priv->top_rss_count++] = all_changes[i];
    }
    
    priv->has_previous = true;
    return 0;
}

static int procmem_snapshot(qmem_service_t *svc, json_builder_t *j) {
    procmem_priv_t *priv = (procmem_priv_t *)svc->priv;
    
    json_object_start(j);
    
    json_key(j, "top_growers");
    json_array_start(j);
    for (int i = 0; i < priv->grower_count; i++) {
        procmem_entry_t *e = &priv->growers[i];
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_string(j, "cmd", e->cmd);
        json_kv_int(j, "rss_kb", e->rss_kb);
        json_kv_int(j, "data_kb", e->data_kb);
        json_kv_int(j, "rss_delta_kb", e->rss_delta_kb);
        json_kv_int(j, "data_delta_kb", e->data_delta_kb);
        json_object_end(j);
    }
    json_array_end(j);
    
    json_key(j, "top_shrinkers");
    json_array_start(j);
    for (int i = 0; i < priv->shrinker_count; i++) {
        procmem_entry_t *e = &priv->shrinkers[i];
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_string(j, "cmd", e->cmd);
        json_kv_int(j, "rss_kb", e->rss_kb);
        json_kv_int(j, "data_kb", e->data_kb);
        json_kv_int(j, "rss_delta_kb", e->rss_delta_kb);
        json_kv_int(j, "data_delta_kb", e->data_delta_kb);
        json_object_end(j);
    }
    json_array_end(j);
    
    json_key(j, "top_rss");
    json_array_start(j);
    for (int i = 0; i < priv->top_rss_count; i++) {
        procmem_entry_t *e = &priv->top_rss[i];
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_string(j, "cmd", e->cmd);
        json_kv_int(j, "rss_kb", e->rss_kb);
        json_kv_int(j, "data_kb", e->data_kb);
        json_kv_int(j, "rss_delta_kb", e->rss_delta_kb);
        json_object_end(j);
    }
    json_array_end(j);
    
    json_object_end(j);
    
    return 0;
}

static void procmem_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("procmem service destroyed");
}

static const qmem_service_ops_t procmem_ops = {
    .init = procmem_init,
    .collect = procmem_collect,
    .snapshot = procmem_snapshot,
    .destroy = procmem_destroy,
};

qmem_service_t procmem_service = {
    .name = "procmem",
    .description = "Per-process memory tracking",
    .ops = &procmem_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

#ifndef NO_PLUGIN_DEFINE
QMEM_PLUGIN_DEFINE("procmem", "1.0", "Process memory monitor", procmem_service);
#endif

int procmem_get_top_growers(procmem_entry_t *entries, int max_entries) {
    int n = g_procmem.grower_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_procmem.growers, n * sizeof(procmem_entry_t));
    return n;
}

int procmem_get_top_shrinkers(procmem_entry_t *entries, int max_entries) {
    int n = g_procmem.shrinker_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_procmem.shrinkers, n * sizeof(procmem_entry_t));
    return n;
}

int procmem_get_top_rss(procmem_entry_t *entries, int max_entries) {
    int n = g_procmem.top_rss_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_procmem.top_rss, n * sizeof(procmem_entry_t));
    return n;
}
