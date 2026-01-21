/*
 * fdmon.c - File descriptor monitoring implementation
 *
 * Tracks per-process FD counts via /proc/PID/fd
 * Detects potential FD leaks by tracking FD growth over time
 */
#define _POSIX_C_SOURCE 200809L
#include "fdmon.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <qmem/plugin.h>
#include <ctype.h>

#define MAX_PROCS 100
#define TOP_COUNT 25

typedef struct {
    pid_t pid;
    int fd_count;
} fd_data_t;

typedef struct {
    /* Current and previous FD data */
    fd_data_t current[MAX_PROCS];
    fd_data_t previous[MAX_PROCS];
    int current_count;
    int previous_count;
    bool has_previous;
    
    /* Initial/baseline FD data (first seen values) */
    fd_data_t initial[MAX_PROCS * 2];
    int initial_count;
    
    /* Results: top consumers and leakers */
    fdmon_entry_t top_consumers[TOP_COUNT];
    int consumer_count;
    
    fdmon_entry_t leakers[TOP_COUNT];
    int leaker_count;
    
    fdmon_summary_t summary;
} fdmon_priv_t;

static fdmon_priv_t g_fdmon;

static int fdmon_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_fdmon, 0, sizeof(g_fdmon));
    svc->priv = &g_fdmon;
    
    log_debug("fdmon service initialized");
    return 0;
}

/* Count FDs for a process and classify by type */
static int count_fds(pid_t pid, fdmon_fd_types_t *types) {
    char fd_path[128];
    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", pid);
    
    DIR *dir = opendir(fd_path);
    if (!dir) return -1;
    
    int count = 0;
    if (types) {
        memset(types, 0, sizeof(*types));
    }
    
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit(ent->d_name[0])) continue;
        count++;
        
        if (types) {
            /* Read link to classify FD type */
            char link_path[512];
            char target[512];
            snprintf(link_path, sizeof(link_path), "/proc/%d/fd/%s", pid, ent->d_name);
            ssize_t len = readlink(link_path, target, sizeof(target) - 1);
            if (len > 0) {
                target[len] = '\0';
                
                if (strncmp(target, "socket:", 7) == 0) {
                    types->sockets++;
                } else if (strncmp(target, "pipe:", 5) == 0) {
                    types->pipes++;
                } else if (strncmp(target, "anon_inode:", 11) == 0) {
                    types->eventfds++;
                } else if (target[0] == '/') {
                    types->files++;
                } else {
                    types->other++;
                }
            }
        }
    }
    
    closedir(dir);
    return count;
}

/* Find previous FD count for a PID */
static fd_data_t *find_previous(fdmon_priv_t *priv, pid_t pid) {
    for (int i = 0; i < priv->previous_count; i++) {
        if (priv->previous[i].pid == pid) {
            return &priv->previous[i];
        }
    }
    return NULL;
}

/* Find or create initial baseline entry */
static fd_data_t *find_or_create_initial(fdmon_priv_t *priv, pid_t pid, int fd_count) {
    for (int i = 0; i < priv->initial_count; i++) {
        if (priv->initial[i].pid == pid) {
            return &priv->initial[i];
        }
    }
    
    /* Create new baseline */
    if (priv->initial_count < MAX_PROCS * 2) {
        fd_data_t *init = &priv->initial[priv->initial_count++];
        init->pid = pid;
        init->fd_count = fd_count;
        return init;
    }
    
    return NULL;
}

/* Compare function for sorting by FD count (descending) */
static int cmp_fd_count_desc(const void *a, const void *b) {
    const fdmon_entry_t *ea = (const fdmon_entry_t *)a;
    const fdmon_entry_t *eb = (const fdmon_entry_t *)b;
    return eb->fd_count - ea->fd_count;
}

/* Compare function for sorting by FD change (descending) */
static int cmp_fd_change_desc(const void *a, const void *b) {
    const fdmon_entry_t *ea = (const fdmon_entry_t *)a;
    const fdmon_entry_t *eb = (const fdmon_entry_t *)b;
    return eb->fd_change - ea->fd_change;
}

static int fdmon_collect(qmem_service_t *svc) {
    fdmon_priv_t *priv = (fdmon_priv_t *)svc->priv;
    
    /* Save previous */
    memcpy(priv->previous, priv->current, sizeof(priv->previous));
    priv->previous_count = priv->current_count;
    
    /* Reset current */
    priv->current_count = 0;
    priv->consumer_count = 0;
    priv->leaker_count = 0;
    memset(&priv->summary, 0, sizeof(priv->summary));
    
    /* Temporary array for all processes */
    fdmon_entry_t all_procs[MAX_PROCS];
    int all_count = 0;
    
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return -1;
    
    struct dirent *ent;
    while ((ent = readdir(proc_dir)) != NULL && all_count < MAX_PROCS) {
        if (!isdigit(ent->d_name[0])) continue;
        
        pid_t pid = atoi(ent->d_name);
        
        fdmon_fd_types_t types;
        int fd_count = count_fds(pid, &types);
        if (fd_count < 0) continue;
        
        /* Store in current array */
        if (priv->current_count < MAX_PROCS) {
            priv->current[priv->current_count].pid = pid;
            priv->current[priv->current_count].fd_count = fd_count;
            priv->current_count++;
        }
        
        /* Build entry */
        fdmon_entry_t *e = &all_procs[all_count++];
        e->pid = pid;
        e->fd_count = fd_count;
        e->types = types;
        
        /* Get command */
        if (proc_read_cmdline(pid, e->cmd, sizeof(e->cmd)) < 0) {
            proc_read_comm(pid, e->cmd, sizeof(e->cmd));
        }
        
        /* Calculate delta from previous sample */
        fd_data_t *prev = find_previous(priv, pid);
        e->fd_delta = prev ? (fd_count - prev->fd_count) : 0;
        
        /* Get or create initial baseline */
        fd_data_t *init = find_or_create_initial(priv, pid, fd_count);
        e->initial_fd_count = init ? init->fd_count : fd_count;
        e->fd_change = fd_count - e->initial_fd_count;
        
        /* Update summary */
        priv->summary.total_fds += fd_count;
        priv->summary.proc_count++;
        if (e->fd_change > 0) {
            priv->summary.potential_leaks++;
        }
    }
    
    closedir(proc_dir);
    
    /* Sort by FD count for top consumers */
    qsort(all_procs, all_count, sizeof(fdmon_entry_t), cmp_fd_count_desc);
    int n = all_count < TOP_COUNT ? all_count : TOP_COUNT;
    memcpy(priv->top_consumers, all_procs, n * sizeof(fdmon_entry_t));
    priv->consumer_count = n;
    
    /* Sort by FD change for potential leakers */
    qsort(all_procs, all_count, sizeof(fdmon_entry_t), cmp_fd_change_desc);
    
    /* Only include those with positive change */
    priv->leaker_count = 0;
    for (int i = 0; i < all_count && priv->leaker_count < TOP_COUNT; i++) {
        if (all_procs[i].fd_change > 0) {
            priv->leakers[priv->leaker_count++] = all_procs[i];
        }
    }
    
    priv->has_previous = true;
    return 0;
}

static int fdmon_snapshot(qmem_service_t *svc, json_builder_t *j) {
    fdmon_priv_t *priv = (fdmon_priv_t *)svc->priv;
    
    json_object_start(j);
    
    /* Summary */
    json_key(j, "summary");
    json_object_start(j);
    json_kv_int(j, "total_fds", priv->summary.total_fds);
    json_kv_int(j, "proc_count", priv->summary.proc_count);
    json_kv_int(j, "potential_leaks", priv->summary.potential_leaks);
    json_object_end(j);
    
    /* Top FD consumers */
    json_key(j, "top_consumers");
    json_array_start(j);
    for (int i = 0; i < priv->consumer_count; i++) {
        fdmon_entry_t *e = &priv->top_consumers[i];
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_string(j, "cmd", e->cmd);
        json_kv_int(j, "fd_count", e->fd_count);
        json_kv_int(j, "initial_fd_count", e->initial_fd_count);
        json_kv_int(j, "fd_change", e->fd_change);
        json_kv_int(j, "fd_delta", e->fd_delta);
        json_kv_int(j, "files", e->types.files);
        json_kv_int(j, "sockets", e->types.sockets);
        json_kv_int(j, "pipes", e->types.pipes);
        json_kv_int(j, "eventfds", e->types.eventfds);
        json_kv_int(j, "other", e->types.other);
        json_object_end(j);
    }
    json_array_end(j);
    
    /* Potential FD leakers */
    json_key(j, "leakers");
    json_array_start(j);
    for (int i = 0; i < priv->leaker_count; i++) {
        fdmon_entry_t *e = &priv->leakers[i];
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_string(j, "cmd", e->cmd);
        json_kv_int(j, "fd_count", e->fd_count);
        json_kv_int(j, "initial_fd_count", e->initial_fd_count);
        json_kv_int(j, "fd_change", e->fd_change);
        json_object_end(j);
    }
    json_array_end(j);
    
    json_object_end(j);
    return 0;
}

static void fdmon_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("fdmon service destroyed");
}

static const qmem_service_ops_t fdmon_ops = {
    .init = fdmon_init,
    .collect = fdmon_collect,
    .snapshot = fdmon_snapshot,
    .destroy = fdmon_destroy,
};

qmem_service_t fdmon_service = {
    .name = "fdmon",
    .description = "File descriptor monitoring and leak detection",
    .ops = &fdmon_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

QMEM_PLUGIN_DEFINE("fdmon", "1.0", "FD monitor and leak detection", fdmon_service);

int fdmon_get_top_consumers(fdmon_entry_t *entries, int max_entries) {
    int n = g_fdmon.consumer_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_fdmon.top_consumers, n * sizeof(fdmon_entry_t));
    return n;
}

int fdmon_get_leakers(fdmon_entry_t *entries, int max_entries) {
    int n = g_fdmon.leaker_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_fdmon.leakers, n * sizeof(fdmon_entry_t));
    return n;
}

const fdmon_summary_t *fdmon_get_summary(void) {
    return &g_fdmon.summary;
}
