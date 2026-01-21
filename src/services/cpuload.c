/*
 * cpuload.c - Per-process CPU load monitor implementation
 * 
 * Reads /proc/stat for system-wide CPU stats
 * Reads /proc/<pid>/stat for per-process CPU usage
 */
#include "cpuload.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>

#define MAX_PROCS 4096
#define TOP_N 20
#define HASH_SIZE 8192

/* Per-process CPU data */
typedef struct proc_cpu {
    pid_t pid;
    unsigned long utime;
    unsigned long stime;
    unsigned long total_time;
    char cmd[128];
    struct proc_cpu *next;
} proc_cpu_t;

/* System CPU counters */
typedef struct {
    unsigned long user;
    unsigned long nice;
    unsigned long system;
    unsigned long idle;
    unsigned long iowait;
    unsigned long irq;
    unsigned long softirq;
    unsigned long total;
} cpu_counters_t;

typedef struct {
    /* System CPU */
    cpu_counters_t curr_sys;
    cpu_counters_t prev_sys;
    cpuload_system_t system_stats;
    
    /* Per-process CPU */
    proc_cpu_t *current[HASH_SIZE];
    proc_cpu_t *previous[HASH_SIZE];
    bool has_previous;
    
    /* Results */
    cpuload_entry_t top_consumers[TOP_N];
    int top_count;
    
    /* Memory pool */
    proc_cpu_t pool[MAX_PROCS * 2];
    int pool_idx;
    
    /* Clock ticks per second */
    long clock_ticks;
} cpuload_priv_t;

static cpuload_priv_t g_cpuload;

static unsigned int hash_pid(pid_t pid) {
    return (unsigned int)pid % HASH_SIZE;
}

static proc_cpu_t *alloc_entry(cpuload_priv_t *priv) {
    if (priv->pool_idx >= MAX_PROCS * 2) return NULL;
    return &priv->pool[priv->pool_idx++];
}

static void clear_hash(proc_cpu_t **hash) {
    memset(hash, 0, sizeof(proc_cpu_t *) * HASH_SIZE);
}

static proc_cpu_t *find_in_hash(proc_cpu_t **hash, pid_t pid) {
    unsigned int idx = hash_pid(pid);
    proc_cpu_t *e = hash[idx];
    while (e) {
        if (e->pid == pid) return e;
        e = e->next;
    }
    return NULL;
}

static void insert_hash(proc_cpu_t **hash, proc_cpu_t *entry) {
    unsigned int idx = hash_pid(entry->pid);
    entry->next = hash[idx];
    hash[idx] = entry;
}

static int cpuload_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_cpuload, 0, sizeof(g_cpuload));
    g_cpuload.clock_ticks = sysconf(_SC_CLK_TCK);
    if (g_cpuload.clock_ticks <= 0) {
        g_cpuload.clock_ticks = 100;  /* Default */
    }
    svc->priv = &g_cpuload;
    
    log_debug("cpuload service initialized (clock_ticks=%ld)", g_cpuload.clock_ticks);
    return 0;
}

static int parse_proc_stat(cpu_counters_t *counters) {
    char buf[1024];
    
    if (proc_read_file("/proc/stat", buf, sizeof(buf)) < 0) {
        return -1;
    }
    
    /* Parse first line: cpu user nice system idle iowait irq softirq ... */
    if (sscanf(buf, "cpu %lu %lu %lu %lu %lu %lu %lu",
               &counters->user, &counters->nice, &counters->system,
               &counters->idle, &counters->iowait, &counters->irq,
               &counters->softirq) != 7) {
        return -1;
    }
    
    counters->total = counters->user + counters->nice + counters->system +
                      counters->idle + counters->iowait + counters->irq +
                      counters->softirq;
    
    return 0;
}

static int parse_pid_stat(pid_t pid, unsigned long *utime, unsigned long *stime, char *cmd, size_t cmd_size) {
    char path[64];
    char buf[1024];
    
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    if (proc_read_file(path, buf, sizeof(buf)) < 0) {
        return -1;
    }
    
    /* Format: pid (comm) state ppid pgrp session tty_nr tpgid flags
     *         minflt cminflt majflt cmajflt utime stime cutime cstime ...
     * Fields 14 and 15 are utime and stime
     */
    
    /* Find comm (in parentheses) */
    char *open_paren = strchr(buf, '(');
    char *close_paren = strrchr(buf, ')');
    if (!open_paren || !close_paren) return -1;
    
    /* Extract command name */
    size_t comm_len = close_paren - open_paren - 1;
    if (comm_len >= cmd_size) comm_len = cmd_size - 1;
    memcpy(cmd, open_paren + 1, comm_len);
    cmd[comm_len] = '\0';
    
    /* Parse after the comm field */
    char *p = close_paren + 2;  /* Skip ") " */
    
    /* Skip to field 14 (utime) - fields after comm are 3..52 */
    int field = 3;
    while (*p && field < 14) {
        while (*p && !isspace(*p)) p++;
        while (*p && isspace(*p)) p++;
        field++;
    }
    
    *utime = strtoul(p, &p, 10);
    while (*p && isspace(*p)) p++;
    *stime = strtoul(p, NULL, 10);
    
    return 0;
}

static int compare_cpu(const void *a, const void *b) {
    const cpuload_entry_t *ea = (const cpuload_entry_t *)a;
    const cpuload_entry_t *eb = (const cpuload_entry_t *)b;
    if (eb->cpu_percent > ea->cpu_percent) return 1;
    if (eb->cpu_percent < ea->cpu_percent) return -1;
    return 0;
}

static int cpuload_collect(qmem_service_t *svc) {
    cpuload_priv_t *priv = (cpuload_priv_t *)svc->priv;
    
    /* Save previous system stats */
    priv->prev_sys = priv->curr_sys;
    
    /* Read current system stats */
    if (parse_proc_stat(&priv->curr_sys) < 0) {
        log_warn("Failed to parse /proc/stat");
    }
    
    /* Calculate system percentages */
    unsigned long total_delta = priv->curr_sys.total - priv->prev_sys.total;
    if (total_delta > 0 && priv->has_previous) {
        priv->system_stats.user_percent = 
            100.0 * (priv->curr_sys.user - priv->prev_sys.user) / total_delta;
        priv->system_stats.system_percent = 
            100.0 * (priv->curr_sys.system - priv->prev_sys.system) / total_delta;
        priv->system_stats.idle_percent = 
            100.0 * (priv->curr_sys.idle - priv->prev_sys.idle) / total_delta;
        priv->system_stats.iowait_percent = 
            100.0 * (priv->curr_sys.iowait - priv->prev_sys.iowait) / total_delta;
    }
    
    /* Swap current to previous process data */
    memcpy(priv->previous, priv->current, sizeof(priv->previous));
    clear_hash(priv->current);
    priv->pool_idx = 0;
    
    /* Collect all processes */
    cpuload_entry_t all_entries[MAX_PROCS];
    int entry_count = 0;
    
    DIR *dir = opendir("/proc");
    if (!dir) return -1;
    
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && entry_count < MAX_PROCS) {
        /* Skip non-numeric entries */
        if (!isdigit(ent->d_name[0])) continue;
        
        pid_t pid = atoi(ent->d_name);
        unsigned long utime, stime;
        char cmd[128];
        
        if (parse_pid_stat(pid, &utime, &stime, cmd, sizeof(cmd)) < 0) {
            continue;
        }
        
        /* Store current data */
        proc_cpu_t *entry = alloc_entry(priv);
        if (!entry) break;
        
        entry->pid = pid;
        entry->utime = utime;
        entry->stime = stime;
        entry->total_time = utime + stime;
        strncpy(entry->cmd, cmd, sizeof(entry->cmd) - 1);
        insert_hash(priv->current, entry);
        
        /* Calculate CPU percentage if we have previous data */
        if (priv->has_previous && total_delta > 0) {
            proc_cpu_t *prev = find_in_hash(priv->previous, pid);
            if (prev) {
                unsigned long proc_delta = entry->total_time - prev->total_time;
                double cpu_pct = 100.0 * proc_delta / total_delta;
                
                if (cpu_pct > 0.01) {  /* Filter out near-zero */
                    cpuload_entry_t *e = &all_entries[entry_count++];
                    e->pid = pid;
                    strncpy(e->cmd, cmd, sizeof(e->cmd) - 1);
                    e->cpu_percent = cpu_pct;
                    e->utime = utime;
                    e->stime = stime;
                }
            }
        }
    }
    closedir(dir);
    
    /* Sort and take top N */
    qsort(all_entries, entry_count, sizeof(cpuload_entry_t), compare_cpu);
    priv->top_count = entry_count > TOP_N ? TOP_N : entry_count;
    memcpy(priv->top_consumers, all_entries, priv->top_count * sizeof(cpuload_entry_t));
    
    priv->has_previous = true;
    return 0;
}

static int cpuload_snapshot(qmem_service_t *svc, json_builder_t *j) {
    cpuload_priv_t *priv = (cpuload_priv_t *)svc->priv;
    
    json_object_start(j);
    
    /* System CPU */
    json_key(j, "system");
    json_object_start(j);
    json_kv_double(j, "user_percent", priv->system_stats.user_percent);
    json_kv_double(j, "system_percent", priv->system_stats.system_percent);
    json_kv_double(j, "idle_percent", priv->system_stats.idle_percent);
    json_kv_double(j, "iowait_percent", priv->system_stats.iowait_percent);
    json_object_end(j);
    
    /* Top CPU consumers */
    json_key(j, "top_consumers");
    json_array_start(j);
    for (int i = 0; i < priv->top_count; i++) {
        cpuload_entry_t *e = &priv->top_consumers[i];
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_string(j, "cmd", e->cmd);
        json_kv_double(j, "cpu_percent", e->cpu_percent);
        json_kv_uint(j, "utime", e->utime);
        json_kv_uint(j, "stime", e->stime);
        json_object_end(j);
    }
    json_array_end(j);
    
    json_object_end(j);
    return 0;
}

static void cpuload_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("cpuload service destroyed");
}

static const qmem_service_ops_t cpuload_ops = {
    .init = cpuload_init,
    .collect = cpuload_collect,
    .snapshot = cpuload_snapshot,
    .destroy = cpuload_destroy,
};

qmem_service_t cpuload_service = {
    .name = "cpuload",
    .description = "Per-process CPU load from /proc/pid/stat",
    .ops = &cpuload_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

int cpuload_get_top(cpuload_entry_t *entries, int max_entries) {
    int n = g_cpuload.top_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_cpuload.top_consumers, n * sizeof(cpuload_entry_t));
    return n;
}

const cpuload_system_t *cpuload_get_system(void) {
    return &g_cpuload.system_stats;
}
