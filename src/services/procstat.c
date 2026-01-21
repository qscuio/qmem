/*
 * procstat.c - Process status monitor implementation
 * 
 * Monitors whether processes/threads are active (R) or blocked (D/S)
 * Reads /proc/<pid>/stat for state and /proc/<pid>/wchan for wait channel
 */
#include "procstat.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

#define MAX_BLOCKED 100

typedef struct {
    procstat_summary_t summary;
    procstat_entry_t blocked[MAX_BLOCKED];
    int blocked_count;
} procstat_priv_t;

static procstat_priv_t g_procstat;

static const char *state_to_desc(char state) {
    switch (state) {
        case 'R': return "Running";
        case 'S': return "Sleeping";
        case 'D': return "Disk Sleep (blocked)";
        case 'Z': return "Zombie";
        case 'T': return "Stopped";
        case 't': return "Tracing stop";
        case 'X': return "Dead";
        case 'I': return "Idle";
        default: return "Unknown";
    }
}

static int procstat_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_procstat, 0, sizeof(g_procstat));
    svc->priv = &g_procstat;
    
    log_debug("procstat service initialized");
    return 0;
}

static int read_proc_state(pid_t pid, pid_t tid, char *state, char *cmd, size_t cmd_size) {
    char path[128];
    char buf[1024];
    
    if (tid > 0 && tid != pid) {
        snprintf(path, sizeof(path), "/proc/%d/task/%d/stat", pid, tid);
    } else {
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    }
    
    if (proc_read_file(path, buf, sizeof(buf)) < 0) {
        return -1;
    }
    
    /* Parse: pid (comm) state ... */
    char *open_paren = strchr(buf, '(');
    char *close_paren = strrchr(buf, ')');
    if (!open_paren || !close_paren) return -1;
    
    /* Extract command */
    size_t comm_len = close_paren - open_paren - 1;
    if (comm_len >= cmd_size) comm_len = cmd_size - 1;
    memcpy(cmd, open_paren + 1, comm_len);
    cmd[comm_len] = '\0';
    
    /* State is after closing paren */
    char *p = close_paren + 2;
    *state = *p;
    
    return 0;
}

static int read_wchan(pid_t pid, pid_t tid, char *wchan, size_t size) {
    char path[128];
    
    if (tid > 0 && tid != pid) {
        snprintf(path, sizeof(path), "/proc/%d/task/%d/wchan", pid, tid);
    } else {
        snprintf(path, sizeof(path), "/proc/%d/wchan", pid);
    }
    
    ssize_t n = proc_read_file(path, wchan, size);
    if (n <= 0) {
        wchan[0] = '\0';
        return -1;
    }
    
    /* Trim newline */
    while (n > 0 && (wchan[n-1] == '\n' || wchan[n-1] == '\r')) {
        wchan[--n] = '\0';
    }
    
    /* "0" means not waiting */
    if (strcmp(wchan, "0") == 0) {
        wchan[0] = '\0';
    }
    
    return 0;
}

static int procstat_collect(qmem_service_t *svc) {
    procstat_priv_t *priv = (procstat_priv_t *)svc->priv;
    
    /* Reset counters */
    memset(&priv->summary, 0, sizeof(priv->summary));
    priv->blocked_count = 0;
    
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return -1;
    
    struct dirent *ent;
    while ((ent = readdir(proc_dir)) != NULL) {
        /* Skip non-numeric entries */
        if (!isdigit(ent->d_name[0])) continue;
        
        pid_t pid = atoi(ent->d_name);
        char state;
        char cmd[128];
        
        if (read_proc_state(pid, 0, &state, cmd, sizeof(cmd)) < 0) {
            continue;
        }
        
        priv->summary.total++;
        
        switch (state) {
            case 'R': priv->summary.running++; break;
            case 'S': priv->summary.sleeping++; break;
            case 'D': priv->summary.disk_sleep++; break;
            case 'Z': priv->summary.zombie++; break;
            case 'T':
            case 't': priv->summary.stopped++; break;
        }
        
        /* Track blocked (D state) processes with details */
        if (state == 'D' && priv->blocked_count < MAX_BLOCKED) {
            procstat_entry_t *e = &priv->blocked[priv->blocked_count++];
            e->pid = pid;
            e->tid = pid;
            strncpy(e->cmd, cmd, sizeof(e->cmd) - 1);
            e->state = state;
            e->state_desc = state_to_desc(state);
            e->is_blocked = true;
            
            read_wchan(pid, 0, e->wchan, sizeof(e->wchan));
            
            /* Also check threads for this process */
            char task_path[128];
            snprintf(task_path, sizeof(task_path), "/proc/%d/task", pid);
            DIR *task_dir = opendir(task_path);
            if (task_dir) {
                struct dirent *task_ent;
                while ((task_ent = readdir(task_dir)) != NULL) {
                    if (!isdigit(task_ent->d_name[0])) continue;
                    
                    pid_t tid = atoi(task_ent->d_name);
                    if (tid == pid) continue;  /* Skip main thread, already counted */
                    
                    char thread_state;
                    char thread_cmd[128];
                    if (read_proc_state(pid, tid, &thread_state, thread_cmd, sizeof(thread_cmd)) == 0) {
                        if (thread_state == 'D' && priv->blocked_count < MAX_BLOCKED) {
                            procstat_entry_t *te = &priv->blocked[priv->blocked_count++];
                            te->pid = pid;
                            te->tid = tid;
                            strncpy(te->cmd, thread_cmd, sizeof(te->cmd) - 1);
                            te->state = thread_state;
                            te->state_desc = state_to_desc(thread_state);
                            te->is_blocked = true;
                            read_wchan(pid, tid, te->wchan, sizeof(te->wchan));
                        }
                    }
                }
                closedir(task_dir);
            }
        }
    }
    
    closedir(proc_dir);
    return 0;
}

static int procstat_snapshot(qmem_service_t *svc, json_builder_t *j) {
    procstat_priv_t *priv = (procstat_priv_t *)svc->priv;
    
    json_object_start(j);
    
    /* Summary */
    json_key(j, "summary");
    json_object_start(j);
    json_kv_int(j, "total", priv->summary.total);
    json_kv_int(j, "running", priv->summary.running);
    json_kv_int(j, "sleeping", priv->summary.sleeping);
    json_kv_int(j, "disk_sleep", priv->summary.disk_sleep);
    json_kv_int(j, "zombie", priv->summary.zombie);
    json_kv_int(j, "stopped", priv->summary.stopped);
    json_object_end(j);
    
    /* Blocked processes */
    json_key(j, "blocked");
    json_array_start(j);
    for (int i = 0; i < priv->blocked_count; i++) {
        procstat_entry_t *e = &priv->blocked[i];
        json_object_start(j);
        json_kv_int(j, "pid", e->pid);
        json_kv_int(j, "tid", e->tid);
        json_kv_string(j, "cmd", e->cmd);
        json_kv_string(j, "state", e->state_desc);
        json_kv_string(j, "wchan", e->wchan);
        json_object_end(j);
    }
    json_array_end(j);
    
    json_object_end(j);
    return 0;
}

static void procstat_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("procstat service destroyed");
}

static const qmem_service_ops_t procstat_ops = {
    .init = procstat_init,
    .collect = procstat_collect,
    .snapshot = procstat_snapshot,
    .destroy = procstat_destroy,
};

qmem_service_t procstat_service = {
    .name = "procstat",
    .description = "Process/thread status (active/blocked)",
    .ops = &procstat_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

const procstat_summary_t *procstat_get_summary(void) {
    return &g_procstat.summary;
}

int procstat_get_blocked(procstat_entry_t *entries, int max_entries) {
    int n = g_procstat.blocked_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_procstat.blocked, n * sizeof(procstat_entry_t));
    return n;
}

int procstat_get_threads(pid_t pid, procstat_entry_t *entries, int max_entries) {
    int count = 0;
    char task_path[128];
    snprintf(task_path, sizeof(task_path), "/proc/%d/task", pid);
    
    DIR *dir = opendir(task_path);
    if (!dir) return -1;
    
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max_entries) {
        if (!isdigit(ent->d_name[0])) continue;
        
        pid_t tid = atoi(ent->d_name);
        char state;
        char cmd[128];
        
        if (read_proc_state(pid, tid, &state, cmd, sizeof(cmd)) == 0) {
            procstat_entry_t *e = &entries[count++];
            e->pid = pid;
            e->tid = tid;
            strncpy(e->cmd, cmd, sizeof(e->cmd) - 1);
            e->state = state;
            e->state_desc = state_to_desc(state);
            e->is_blocked = (state == 'D');
            read_wchan(pid, tid, e->wchan, sizeof(e->wchan));
        }
    }
    
    closedir(dir);
    return count;
}
