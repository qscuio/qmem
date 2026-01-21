/*
 * daemon.c - Daemon lifecycle management
 */
#define _POSIX_C_SOURCE 200809L

#include "daemon.h"
#include "service_manager.h"
#include "ipc_server.h"
#include "ringbuffer.h"
#include "common/log.h"
#include "common/json.h"

#include "services/meminfo.h"
#include "services/slabinfo.h"
#include "services/procmem.h"
#include "services/heapmon.h"
#include "services/vmstat.h"
#include "services/cpuload.h"
#include "services/netstat.h"
#include "services/procstat.h"
#include "services/sockstat.h"
#include "services/procevent.h"

#ifdef QMEM_WEB_ENABLED
#include "web/http_server.h"
#include "web/api.h"
#endif

#include "plugin_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

static volatile int g_running = 0;
static volatile int g_reload = 0;
static qmem_config_t g_config;
static ringbuf_t *g_history = NULL;
static char g_current_snapshot[256 * 1024];

static void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            g_running = 0;
            break;
        case SIGHUP:
            g_reload = 1;
            break;
    }
}

static int daemonize(const qmem_config_t *cfg) {
    if (cfg->foreground) {
        return 0;
    }
    
    /* First fork */
    pid_t pid = fork();
    if (pid < 0) {
        log_error("fork() failed: %s", strerror(errno));
        return -1;
    }
    if (pid > 0) {
        exit(0);  /* Parent exits */
    }
    
    /* Create new session */
    if (setsid() < 0) {
        log_error("setsid() failed: %s", strerror(errno));
        return -1;
    }
    
    /* Second fork */
    pid = fork();
    if (pid < 0) {
        log_error("fork() failed: %s", strerror(errno));
        return -1;
    }
    if (pid > 0) {
        exit(0);
    }
    
    /* Change working directory */
    if (chdir("/") < 0) {
        log_warn("chdir(/) failed: %s", strerror(errno));
    }
    
    /* Reset umask */
    umask(0);
    
    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    /* Redirect to /dev/null */
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
    
    return 0;
}

static int write_pidfile(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("Cannot create pidfile %s: %s", path, strerror(errno));
        return -1;
    }
    
    fprintf(f, "%d\n", getpid());
    fclose(f);
    return 0;
}

static const char *get_snapshot_callback(void) {
    return g_current_snapshot;
}

static const char *get_history_callback(int count) {
    static char history_buf[1024 * 1024];
    json_builder_t json;
    json_init(&json, history_buf, sizeof(history_buf));
    
    json_object_start(&json);
    json_key(&json, "history");
    json_array_start(&json);
    
    int n = ringbuf_count(g_history);
    if (count > n) count = n;
    
    for (int i = 0; i < count; i++) {
        const ringbuf_entry_t *entry = ringbuf_get_recent(g_history, i);
        if (entry && entry->data) {
            /* Write raw JSON data */
            json_object_start(&json);
            json_kv_int(&json, "timestamp", (int64_t)entry->timestamp);
            /* We can't embed raw JSON, so just note the offset */
            json_kv_int(&json, "index", i);
            json_object_end(&json);
        }
    }
    
    json_array_end(&json);
    json_object_end(&json);
    
    return history_buf;
}

int daemon_init(const qmem_config_t *cfg) {
    memcpy(&g_config, cfg, sizeof(g_config));
    
    /* Set up signal handlers */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    
    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
    
    /* Daemonize if needed */
    if (daemonize(cfg) < 0) {
        return -1;
    }
    
    /* Switch to syslog if running as daemon */
    if (!cfg->foreground) {
        log_init(cfg->log_level, true, "qmemd");
    }
    
    /* Write pidfile */
    if (strlen(cfg->pidfile) > 0) {
        write_pidfile(cfg->pidfile);
    }
    
    /* Create history buffer */
    g_history = ringbuf_create(cfg->max_snapshots);
    if (!g_history) {
        log_error("Failed to create history buffer");
        return -1;
    }
    
    /* Initialize service manager */
    if (svc_manager_init(cfg) < 0) {
        return -1;
    }
    
    /* Register services */
    if (cfg->svc_meminfo) svc_manager_register(&meminfo_service);
    if (cfg->svc_slabinfo) svc_manager_register(&slabinfo_service);
    if (cfg->svc_procmem) svc_manager_register(&procmem_service);
    if (cfg->svc_heapmon) svc_manager_register(&heapmon_service);
    if (cfg->svc_vmstat) svc_manager_register(&vmstat_service);
    if (cfg->svc_cpuload) svc_manager_register(&cpuload_service);
    if (cfg->svc_netstat) svc_manager_register(&netstat_service);
    if (cfg->svc_procstat) svc_manager_register(&procstat_service);
    
    /* Always register these services */
    svc_manager_register(&sockstat_service);
    svc_manager_register(&procevent_service);
    
    /* Start IPC server */
    ipc_set_snapshot_callback(get_snapshot_callback);
    ipc_set_history_callback(get_history_callback);
    if (ipc_server_start(cfg) < 0) {
        log_warn("Failed to start IPC server");
    }
    
#ifdef QMEM_WEB_ENABLED
    /* Start HTTP server */
    api_set_snapshot_callback(get_snapshot_callback);
    api_init();
    if (http_server_start(cfg) < 0) {
        log_warn("Failed to start HTTP server");
    }
#endif
    
    /* Initialize and load plugins */
    if (cfg->enable_plugins) {
        plugin_loader_init(cfg);
        plugin_loader_load_all();
        plugin_loader_start_watcher();
    }
    
    log_info("Daemon initialized (pid=%d, interval=%ds)", getpid(), cfg->interval_sec);
    return 0;
}

int daemon_run(void) {
    g_running = 1;
    
    log_info("Starting monitoring loop");
    
    while (g_running) {
        /* Collect from all services */
        svc_manager_collect_all();
        
        /* Generate snapshot */
        json_builder_t json;
        json_init(&json, g_current_snapshot, sizeof(g_current_snapshot));
        svc_manager_snapshot_all(&json);
        
        /* Store in history */
        ringbuf_push(g_history, g_current_snapshot, json_length(&json));
        
        log_debug("Collected snapshot (%zu bytes)", json_length(&json));
        
        /* Handle reload request */
        if (g_reload) {
            g_reload = 0;
            log_info("Reloading configuration...");
            /* TODO: Implement config reload */
        }
        
        /* Check for plugin updates */
        if (g_config.enable_plugins) {
            plugin_loader_check_updates();
        }
        
        /* Sleep */
        for (int i = 0; i < g_config.interval_sec && g_running; i++) {
            sleep(1);
        }
    }
    
    return 0;
}

void daemon_shutdown(void) {
    log_info("Shutting down daemon...");
    
    g_running = 0;
    
#ifdef QMEM_WEB_ENABLED
    /* Stop HTTP server */
    http_server_stop();
#endif
    
    /* Shutdown plugin loader */
    if (g_config.enable_plugins) {
        plugin_loader_shutdown();
    }
    
    /* Stop IPC server */
    ipc_server_stop();
    
    /* Shutdown services */
    svc_manager_shutdown();
    
    /* Free history */
    if (g_history) {
        ringbuf_destroy(g_history);
        g_history = NULL;
    }
    
    /* Remove pidfile */
    if (strlen(g_config.pidfile) > 0) {
        unlink(g_config.pidfile);
    }
    
    log_info("Daemon shutdown complete");
    log_shutdown();
}

void daemon_reload(void) {
    g_reload = 1;
}

int daemon_is_running(void) {
    return g_running;
}
