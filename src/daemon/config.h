/*
 * config.h - Configuration parsing
 */
#ifndef QMEM_CONFIG_H
#define QMEM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct qmem_config {
    /* Daemon settings */
    int interval_sec;
    bool foreground;
    char pidfile[256];
    char socket_path[256];
    int log_level;
    
    /* Thresholds */
    int64_t proc_min_delta_kb;
    int64_t slab_min_delta_kb;
    int proc_top_n;
    int slab_top_n;
    int heap_scan_top_n;
    
    /* Services */
    bool svc_meminfo;
    bool svc_slabinfo;
    bool svc_procmem;
    bool svc_heapmon;
    bool svc_vmstat;
    bool svc_cpuload;
    bool svc_netstat;
    bool svc_procstat;
    
    /* Web server */
    bool web_enabled;
    char web_listen[64];
    int web_port;
    
    /* Plugins */
    bool enable_plugins;
    char plugin_dir[256];
    
    /* History */
    int max_snapshots;
} qmem_config_t;

/* Initialize config with defaults */
void config_init_defaults(qmem_config_t *cfg);

/* Load config from file */
int config_load(qmem_config_t *cfg, const char *path);

/* Parse command-line arguments (overrides file config) */
int config_parse_args(qmem_config_t *cfg, int argc, char **argv);

/* Print usage */
void config_print_usage(const char *prog);

#endif /* QMEM_CONFIG_H */
