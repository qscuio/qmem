/*
 * procevent.c - Process event monitor
 * 
 * Uses proc connector (netlink) to monitor fork/exec/exit events.
 * Falls back to /proc scanning if netlink is unavailable.
 */
#include "procevent.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <qmem/plugin.h>
#include <time.h>

#define MAX_EVENTS 100
#define HASH_SIZE 4096

/* Simple hash table for tracking PIDs */
typedef struct pid_entry {
    pid_t pid;
    char cmd[64];
    struct pid_entry *next;
} pid_entry_t;

typedef struct {
    proc_event_counters_t counters;
    proc_event_t events[MAX_EVENTS];
    int event_count;
    int event_head;
    
    /* Previous snapshot of PIDs */
    pid_entry_t *prev_pids[HASH_SIZE];
    pid_entry_t *curr_pids[HASH_SIZE];
    pid_entry_t pool[8192];
    int pool_idx;
    bool has_previous;
} procevent_priv_t;

static procevent_priv_t g_procevent;

static unsigned int hash_pid(pid_t pid) {
    return (unsigned int)pid % HASH_SIZE;
}

static void clear_hash(pid_entry_t **hash) {
    memset(hash, 0, sizeof(pid_entry_t *) * HASH_SIZE);
}

static pid_entry_t *alloc_entry(procevent_priv_t *priv) {
    if (priv->pool_idx >= 8192) return NULL;
    return &priv->pool[priv->pool_idx++];
}

static pid_entry_t *find_pid(pid_entry_t **hash, pid_t pid) {
    unsigned int idx = hash_pid(pid);
    pid_entry_t *e = hash[idx];
    while (e) {
        if (e->pid == pid) return e;
        e = e->next;
    }
    return NULL;
}

static void insert_pid(pid_entry_t **hash, pid_entry_t *entry) {
    unsigned int idx = hash_pid(entry->pid);
    entry->next = hash[idx];
    hash[idx] = entry;
}

static void add_event(procevent_priv_t *priv, proc_event_type_t type, 
                      pid_t pid, pid_t ppid, const char *cmd, int exit_code) {
    proc_event_t *e = &priv->events[priv->event_head];
    e->type = type;
    e->pid = pid;
    e->parent_pid = ppid;
    e->exit_code = exit_code;
    if (cmd) {
        size_t len = strlen(cmd);
        if (len >= sizeof(e->cmd)) len = sizeof(e->cmd) - 1;
        memmove(e->cmd, cmd, len);
        e->cmd[len] = '\0';
    } else {
        e->cmd[0] = '\0';
    }
    e->timestamp = (uint64_t)time(NULL);
    
    priv->event_head = (priv->event_head + 1) % MAX_EVENTS;
    if (priv->event_count < MAX_EVENTS) priv->event_count++;
    
    switch (type) {
        case PROC_EVENT_FORK: priv->counters.forks++; break;
        case PROC_EVENT_EXEC: priv->counters.execs++; break;
        case PROC_EVENT_EXIT: priv->counters.exits++; break;
    }
}

static int procevent_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    memset(&g_procevent, 0, sizeof(g_procevent));
    svc->priv = &g_procevent;
    log_debug("procevent service initialized (using /proc scan fallback)");
    return 0;
}

static int procevent_collect(qmem_service_t *svc) {
    procevent_priv_t *priv = (procevent_priv_t *)svc->priv;
    
    /* Swap current to previous */
    memcpy(priv->prev_pids, priv->curr_pids, sizeof(priv->prev_pids));
    clear_hash(priv->curr_pids);
    priv->pool_idx = 0;
    
    /* Scan /proc for current PIDs */
    DIR *dir = opendir("/proc");
    if (!dir) return -1;
    
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit(ent->d_name[0])) continue;
        
        pid_t pid = atoi(ent->d_name);
        pid_entry_t *e = alloc_entry(priv);
        if (!e) break;
        
        e->pid = pid;
        
        /* Get command name */
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);
        char cmd[64];
        if (proc_read_file(path, cmd, sizeof(cmd)) > 0) {
            /* Trim newline */
            char *nl = strchr(cmd, '\n');
            if (nl) *nl = '\0';
            snprintf(e->cmd, sizeof(e->cmd), "%s", cmd);
        }
        
        insert_pid(priv->curr_pids, e);
        
        /* Check if new process */
        if (priv->has_previous && !find_pid(priv->prev_pids, pid)) {
            add_event(priv, PROC_EVENT_FORK, pid, 0, e->cmd, 0);
        }
    }
    closedir(dir);
    
    /* Find exited processes */
    if (priv->has_previous) {
        for (int i = 0; i < HASH_SIZE; i++) {
            pid_entry_t *e = priv->prev_pids[i];
            while (e) {
                if (!find_pid(priv->curr_pids, e->pid)) {
                    add_event(priv, PROC_EVENT_EXIT, e->pid, 0, e->cmd, 0);
                }
                e = e->next;
            }
        }
    }
    
    priv->has_previous = true;
    return 0;
}

static int procevent_snapshot(qmem_service_t *svc, json_builder_t *j) {
    procevent_priv_t *priv = (procevent_priv_t *)svc->priv;
    
    json_object_start(j);
    
    json_key(j, "counters");
    json_object_start(j);
    json_kv_uint(j, "forks", priv->counters.forks);
    json_kv_uint(j, "execs", priv->counters.execs);
    json_kv_uint(j, "exits", priv->counters.exits);
    json_object_end(j);
    
    json_key(j, "recent_events");
    json_array_start(j);
    
    /* Output last N events */
    int count = priv->event_count > 20 ? 20 : priv->event_count;
    for (int i = 0; i < count; i++) {
        int idx = (priv->event_head - 1 - i + MAX_EVENTS) % MAX_EVENTS;
        proc_event_t *e = &priv->events[idx];
        
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_string(j, "cmd", e->cmd);
        
        const char *type_str = "unknown";
        switch (e->type) {
            case PROC_EVENT_FORK: type_str = "fork"; break;
            case PROC_EVENT_EXEC: type_str = "exec"; break;
            case PROC_EVENT_EXIT: type_str = "exit"; break;
        }
        json_kv_string(j, "type", type_str);
        json_kv_uint(j, "timestamp", e->timestamp);
        json_object_end(j);
    }
    
    json_array_end(j);
    json_object_end(j);
    return 0;
}

static void procevent_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("procevent service destroyed");
}

static const qmem_service_ops_t procevent_ops = {
    .init = procevent_init,
    .collect = procevent_collect,
    .snapshot = procevent_snapshot,
    .destroy = procevent_destroy,
};

qmem_service_t procevent_service = {
    .name = "procevent",
    .description = "Process fork/exit events via /proc scanning",
    .ops = &procevent_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

QMEM_PLUGIN_DEFINE("procevent", "1.0", "Process event monitor", procevent_service);

const proc_event_counters_t *procevent_get_counters(void) {
    return &g_procevent.counters;
}

int procevent_get_recent(proc_event_t *events, int max_events) {
    int count = g_procevent.event_count;
    if (count > max_events) count = max_events;
    
    for (int i = 0; i < count; i++) {
        int idx = (g_procevent.event_head - 1 - i + MAX_EVENTS) % MAX_EVENTS;
        events[i] = g_procevent.events[idx];
    }
    
    return count;
}
