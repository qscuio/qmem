/*
 * config.c - Configuration parsing implementation
 */
#include "config.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>

void config_init_defaults(qmem_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    
    cfg->interval_sec = 10;
    cfg->foreground = false;
    strncpy(cfg->pidfile, "/run/qmem.pid", sizeof(cfg->pidfile) - 1);
    strncpy(cfg->socket_path, "/run/qmem.sock", sizeof(cfg->socket_path) - 1);
    cfg->log_level = QMEM_LOG_INFO;
    
    cfg->proc_min_delta_kb = 1024;
    cfg->slab_min_delta_kb = 512;
    cfg->proc_top_n = 12;
    cfg->slab_top_n = 20;
    cfg->heap_scan_top_n = 12;
    
    cfg->svc_meminfo = true;
    cfg->svc_slabinfo = true;
    cfg->svc_procmem = true;
    cfg->svc_heapmon = true;
    cfg->svc_vmstat = true;
    cfg->svc_cpuload = true;
    cfg->svc_netstat = true;
    cfg->svc_procstat = true;
    
    cfg->web_enabled = true;
    strncpy(cfg->web_listen, "0.0.0.0", sizeof(cfg->web_listen) - 1);
    cfg->web_port = 8080;
    
    cfg->enable_plugins = true;
    strncpy(cfg->plugin_dir, "/usr/lib/qmem/plugins", sizeof(cfg->plugin_dir) - 1);
    
    cfg->max_snapshots = 360;
}

static char *trim(char *str) {
    while (*str && isspace(*str)) str++;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) *end-- = '\0';
    return str;
}

static bool parse_bool(const char *str) {
    return strcmp(str, "true") == 0 || strcmp(str, "1") == 0 || strcmp(str, "yes") == 0;
}

int config_load(qmem_config_t *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_warn("Config file not found: %s (using defaults)", path);
        return 0;  /* Not an error, use defaults */
    }
    
    char line[512];
    char section[64] = "";
    
    while (fgets(line, sizeof(line), f)) {
        char *l = trim(line);
        
        /* Skip comments and empty lines */
        if (*l == '\0' || *l == '#' || *l == ';') continue;
        
        /* Section header */
        if (*l == '[') {
            char *end = strchr(l, ']');
            if (end) {
                *end = '\0';
                strncpy(section, l + 1, sizeof(section) - 1);
            }
            continue;
        }
        
        /* Key = value */
        char *eq = strchr(l, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char *key = trim(l);
        char *val = trim(eq + 1);
        
        /* Parse based on section */
        if (strcmp(section, "daemon") == 0) {
            if (strcmp(key, "interval") == 0) cfg->interval_sec = atoi(val);
            else if (strcmp(key, "foreground") == 0) cfg->foreground = parse_bool(val);
            else if (strcmp(key, "pidfile") == 0) strncpy(cfg->pidfile, val, sizeof(cfg->pidfile) - 1);
            else if (strcmp(key, "socket") == 0) strncpy(cfg->socket_path, val, sizeof(cfg->socket_path) - 1);
            else if (strcmp(key, "log_level") == 0) {
                if (strcmp(val, "debug") == 0) cfg->log_level = QMEM_LOG_DEBUG;
                else if (strcmp(val, "info") == 0) cfg->log_level = QMEM_LOG_INFO;
                else if (strcmp(val, "warn") == 0) cfg->log_level = QMEM_LOG_WARN;
                else if (strcmp(val, "error") == 0) cfg->log_level = QMEM_LOG_ERROR;
            }
        } else if (strcmp(section, "thresholds") == 0) {
            if (strcmp(key, "proc_min_delta_kb") == 0) cfg->proc_min_delta_kb = atoll(val);
            else if (strcmp(key, "slab_min_delta_kb") == 0) cfg->slab_min_delta_kb = atoll(val);
            else if (strcmp(key, "proc_top_n") == 0) cfg->proc_top_n = atoi(val);
            else if (strcmp(key, "slab_top_n") == 0) cfg->slab_top_n = atoi(val);
            else if (strcmp(key, "heap_scan_top_n") == 0) cfg->heap_scan_top_n = atoi(val);
        } else if (strcmp(section, "services") == 0) {
            if (strcmp(key, "meminfo") == 0) cfg->svc_meminfo = parse_bool(val);
            else if (strcmp(key, "slabinfo") == 0) cfg->svc_slabinfo = parse_bool(val);
            else if (strcmp(key, "procmem") == 0) cfg->svc_procmem = parse_bool(val);
            else if (strcmp(key, "heapmon") == 0) cfg->svc_heapmon = parse_bool(val);
            else if (strcmp(key, "vmstat") == 0) cfg->svc_vmstat = parse_bool(val);
        } else if (strcmp(section, "web") == 0) {
            if (strcmp(key, "enabled") == 0) cfg->web_enabled = parse_bool(val);
            else if (strcmp(key, "listen") == 0) strncpy(cfg->web_listen, val, sizeof(cfg->web_listen) - 1);
            else if (strcmp(key, "port") == 0) cfg->web_port = atoi(val);
        } else if (strcmp(section, "history") == 0) {
            if (strcmp(key, "max_snapshots") == 0) cfg->max_snapshots = atoi(val);
        }
    }
    
    fclose(f);
    log_info("Loaded config from %s", path);
    return 0;
}

int config_parse_args(qmem_config_t *cfg, int argc, char **argv) {
    static struct option long_options[] = {
        {"config",     required_argument, 0, 'c'},
        {"foreground", no_argument,       0, 'f'},
        {"interval",   required_argument, 0, 'i'},
        {"socket",     required_argument, 0, 's'},
        {"port",       required_argument, 0, 'p'},
        {"debug",      no_argument,       0, 'd'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    char *config_file = NULL;
    
    /* First pass: find config file */
    optind = 1;
    while ((opt = getopt_long(argc, argv, "c:fi:s:p:dh", long_options, NULL)) != -1) {
        if (opt == 'c') {
            config_file = optarg;
        }
    }
    
    /* Load config file if specified */
    if (config_file) {
        if (config_load(cfg, config_file) < 0) {
            return -1;
        }
    }
    
    /* Second pass: override with command-line options */
    optind = 1;
    while ((opt = getopt_long(argc, argv, "c:fi:s:p:dh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                /* Already handled */
                break;
            case 'f':
                cfg->foreground = true;
                break;
            case 'i':
                cfg->interval_sec = atoi(optarg);
                break;
            case 's':
                strncpy(cfg->socket_path, optarg, sizeof(cfg->socket_path) - 1);
                break;
            case 'p':
                cfg->web_port = atoi(optarg);
                break;
            case 'd':
                cfg->log_level = QMEM_LOG_DEBUG;
                break;
            case 'h':
                config_print_usage(argv[0]);
                exit(0);
            default:
                return -1;
        }
    }
    
    return 0;
}

void config_print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -c, --config FILE    Load configuration from FILE\n");
    printf("  -f, --foreground     Run in foreground (don't daemonize)\n");
    printf("  -i, --interval SEC   Monitoring interval in seconds\n");
    printf("  -s, --socket PATH    Unix socket path for IPC\n");
    printf("  -p, --port PORT      Web server port\n");
    printf("  -d, --debug          Enable debug logging\n");
    printf("  -h, --help           Show this help\n");
}
