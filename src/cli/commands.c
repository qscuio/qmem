/*
 * commands.c - CLI command implementations
 */
#include "commands.h"
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

/* ANSI colors */
#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define YELLOW  "\033[1;33m"
#define CYAN    "\033[0;36m"
#define NC      "\033[0m"

/* Simple JSON value extraction (no external dependency) */
static const char *json_find_key(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    return strstr(json, search);
}

static int64_t json_get_int(const char *json, const char *key) {
    const char *pos = json_find_key(json, key);
    if (!pos) return 0;
    pos = strchr(pos, ':');
    if (!pos) return 0;
    pos++;
    while (*pos && isspace(*pos)) pos++;
    return strtoll(pos, NULL, 10);
}

static double json_get_double(const char *json, const char *key) {
    const char *pos = json_find_key(json, key);
    if (!pos) return 0.0;
    pos = strchr(pos, ':');
    if (!pos) return 0.0;
    pos++;
    while (*pos && isspace(*pos)) pos++;
    return strtod(pos, NULL);
}

static void format_kb(char *buf, size_t size, int64_t kb) {
    if (kb >= 1048576) {
        snprintf(buf, size, "%.2f GB", (double)kb / 1048576.0);
    } else if (kb >= 1024) {
        snprintf(buf, size, "%.2f MB", (double)kb / 1024.0);
    } else {
        snprintf(buf, size, "%ld KB", (long)kb);
    }
}

static void format_delta(char *buf, size_t size, int64_t delta) {
    char value[32];
    int64_t abs_delta = delta < 0 ? -delta : delta;
    format_kb(value, sizeof(value), abs_delta);
    
    if (delta > 0) {
        snprintf(buf, size, RED "+%s" NC, value);
    } else if (delta < 0) {
        snprintf(buf, size, GREEN "-%s" NC, value);
    } else {
        snprintf(buf, size, "0");
    }
}

static void print_separator(void) {
    printf("================================================================================\n");
}

int cmd_status(const char *socket_path) {
    char *response = client_get_snapshot(socket_path);
    if (!response) {
        fprintf(stderr, "Error: Cannot connect to daemon at %s\n", socket_path);
        return 1;
    }
    
    /* Parse and display meminfo */
    const char *meminfo = strstr(response, "\"meminfo\":");
    if (meminfo) {
        double usage = json_get_double(meminfo, "usage_percent");
        
        printf("\n" YELLOW "=== QMem Memory Status ===" NC "\n");
        printf("Memory Usage: " YELLOW "%.2f%%" NC "\n\n", usage);
        
        /* Memory section */
        const char *memory = strstr(meminfo, "\"memory\":");
        if (memory) {
            int64_t total = json_get_int(memory, "\"value\"");  /* First value is total */
            char buf[32];
            format_kb(buf, sizeof(buf), total);
            printf("Total:     %s\n", buf);
            
            /* Find available */
            const char *avail = strstr(memory, "\"available_kb\":");
            if (avail) {
                int64_t val = json_get_int(avail, "value");
                int64_t delta = json_get_int(avail, "delta");
                format_kb(buf, sizeof(buf), val);
                char delta_buf[64];
                format_delta(delta_buf, sizeof(delta_buf), delta);
                printf("Available: %s (%s)\n", buf, delta_buf);
            }
        }
    }
    
    /* CPU Load */
    const char *cpuload = strstr(response, "\"cpuload\":");
    if (cpuload) {
        printf("\n" CYAN "=== CPU ===" NC "\n");
        const char *sys = strstr(cpuload, "\"system\":");
        if (sys) {
            double user = json_get_double(sys, "user_percent");
            double system = json_get_double(sys, "system_percent");
            double idle = json_get_double(sys, "idle_percent");
            double iowait = json_get_double(sys, "iowait_percent");
            printf("User: %.1f%%  System: %.1f%%  Idle: %.1f%%  IOWait: %.1f%%\n",
                   user, system, idle, iowait);
        }
    }
    
    /* Network */
    const char *netstat = strstr(response, "\"netstat\":");
    if (netstat) {
        printf("\n" CYAN "=== Network ===" NC "\n");
        printf("%-10s %12s %12s %10s %10s\n", "Interface", "RX bytes", "TX bytes", "RX rate", "TX rate");
        
        const char *ifaces = strstr(netstat, "\"interfaces\":");
        if (ifaces) {
            const char *pos = strchr(ifaces, '[');
            while (pos && (pos = strstr(pos, "{\"name\":")) != NULL) {
                /* Extract name */
                const char *name_pos = strstr(pos, "\"name\":\"");
                char name[32] = "";
                if (name_pos) {
                    name_pos += 8;
                    const char *end = strchr(name_pos, '"');
                    if (end) {
                        size_t len = end - name_pos;
                        if (len > 31) len = 31;
                        strncpy(name, name_pos, len);
                        name[len] = '\0';
                    }
                }
                
                int64_t rx = json_get_int(pos, "rx_bytes");
                int64_t tx = json_get_int(pos, "tx_bytes");
                double rx_rate = json_get_double(pos, "rx_rate");
                double tx_rate = json_get_double(pos, "tx_rate");
                
                char rx_buf[32], tx_buf[32];
                format_kb(rx_buf, sizeof(rx_buf), rx / 1024);
                format_kb(tx_buf, sizeof(tx_buf), tx / 1024);
                
                printf("%-10s %12s %12s %8.1f/s %8.1f/s\n", name, rx_buf, tx_buf, rx_rate, tx_rate);
                pos++;
            }
        }
    }
    
    /* Socket Stats */
    const char *sockstat = strstr(response, "\"sockstat\":");
    if (sockstat) {
        printf("\n" CYAN "=== Sockets ===" NC "\n");
        const char *tcp = strstr(sockstat, "\"tcp\":");
        if (tcp) {
            int64_t total = json_get_int(tcp, "total");
            int64_t estab = json_get_int(tcp, "established");
            int64_t tw = json_get_int(tcp, "time_wait");
            int64_t listen = json_get_int(tcp, "listen");
            printf("TCP: %ld (ESTAB:%ld TIME_WAIT:%ld LISTEN:%ld)  ",
                   (long)total, (long)estab, (long)tw, (long)listen);
        }
        int64_t udp = json_get_int(sockstat, "udp_total");
        int64_t unix_sock = json_get_int(sockstat, "unix_total");
        printf("UDP: %ld  Unix: %ld\n", (long)udp, (long)unix_sock);
    }
    
    /* Process Stats */
    const char *procstat = strstr(response, "\"procstat\":");
    if (procstat) {
        printf("\n" CYAN "=== Processes ===" NC "\n");
        const char *sum = strstr(procstat, "\"summary\":");
        if (sum) {
            int64_t total = json_get_int(sum, "total");
            int64_t running = json_get_int(sum, "running");
            int64_t sleeping = json_get_int(sum, "sleeping");
            int64_t blocked = json_get_int(sum, "blocked");
            int64_t zombie = json_get_int(sum, "zombie");
            printf("Total: %ld  Running: %ld  Sleeping: %ld  Blocked: %ld  Zombie: %ld\n",
                   (long)total, (long)running, (long)sleeping, (long)blocked, (long)zombie);
        }
    }
    
    /* Process Events */
    const char *procevent = strstr(response, "\"procevent\":");
    if (procevent) {
        const char *counters = strstr(procevent, "\"counters\":");
        if (counters) {
            int64_t forks = json_get_int(counters, "forks");
            int64_t exits = json_get_int(counters, "exits");
            printf("Events: Forks: %ld  Exits: %ld\n", (long)forks, (long)exits);
        }
    }
    
    print_separator();
    return 0;
}

int cmd_top(const char *socket_path) {
    char *response = client_get_snapshot(socket_path);
    if (!response) {
        fprintf(stderr, "Error: Cannot connect to daemon at %s\n", socket_path);
        return 1;
    }
    
    printf("\n" CYAN "=== Top Memory Growers ===" NC "\n\n");
    
    /* Find procmem section */
    const char *procmem = strstr(response, "\"procmem\":");
    if (!procmem) {
        printf("No process data available.\n");
        return 0;
    }
    
    const char *growers = strstr(procmem, "\"top_growers\":");
    if (!growers) {
        printf("No growers data.\n");
        return 0;
    }
    
    printf("%-8s %-12s %-12s %s\n", "PID", "RSS Delta", "RSS Now", "Command");
    print_separator();
    
    /* Parse array (simplified) */
    const char *pos = strchr(growers, '[');
    if (!pos) return 0;
    
    while ((pos = strstr(pos, "{\"pid\":")) != NULL) {
        int64_t pid = json_get_int(pos, "pid");
        int64_t rss = json_get_int(pos, "rss_kb");
        int64_t delta = json_get_int(pos, "rss_delta_kb");
        
        /* Extract cmd */
        const char *cmd_pos = strstr(pos, "\"cmd\":\"");
        char cmd[64] = "unknown";
        if (cmd_pos) {
            cmd_pos += 7;
            const char *end = strchr(cmd_pos, '"');
            if (end) {
                size_t len = end - cmd_pos;
                if (len > 63) len = 63;
                strncpy(cmd, cmd_pos, len);
                cmd[len] = '\0';
            }
        }
        
        char delta_buf[64], rss_buf[32];
        format_delta(delta_buf, sizeof(delta_buf), delta);
        format_kb(rss_buf, sizeof(rss_buf), rss);
        
        printf("%-8ld %-20s %-12s %.40s\n", (long)pid, delta_buf, rss_buf, cmd);
        
        pos++;
    }
    
    return 0;
}

int cmd_slab(const char *socket_path) {
    char *response = client_get_snapshot(socket_path);
    if (!response) {
        fprintf(stderr, "Error: Cannot connect to daemon at %s\n", socket_path);
        return 1;
    }
    
    printf("\n" CYAN "=== Slab Cache Changes ===" NC "\n\n");
    
    const char *slabinfo = strstr(response, "\"slabinfo\":");
    if (!slabinfo) {
        printf("No slab data available.\n");
        return 0;
    }
    
    printf(YELLOW "Top Growers:" NC "\n");
    printf("%-32s %-12s %-12s\n", "Cache", "Delta", "Current");
    print_separator();
    
    const char *growers = strstr(slabinfo, "\"top_growers\":");
    if (growers) {
        const char *pos = strchr(growers, '[');
        while (pos && (pos = strstr(pos, "{\"name\":")) != NULL) {
            /* Extract name */
            const char *name_pos = strstr(pos, "\"name\":\"");
            char name[64] = "unknown";
            if (name_pos) {
                name_pos += 8;
                const char *end = strchr(name_pos, '"');
                if (end) {
                    size_t len = end - name_pos;
                    if (len > 63) len = 63;
                    strncpy(name, name_pos, len);
                    name[len] = '\0';
                }
            }
            
            int64_t size = json_get_int(pos, "size_bytes");
            int64_t delta = json_get_int(pos, "delta_bytes");
            
            char delta_buf[64], size_buf[32];
            format_delta(delta_buf, sizeof(delta_buf), delta / 1024);
            format_kb(size_buf, sizeof(size_buf), size / 1024);
            
            printf("%-32s %-20s %-12s\n", name, delta_buf, size_buf);
            pos++;
        }
    }
    
    return 0;
}

int cmd_watch(const char *socket_path, int interval) {
    printf("Watching memory changes (Ctrl+C to stop)...\n");
    printf("Interval: %d seconds\n\n", interval);
    
    while (1) {
        /* Clear screen */
        printf("\033[2J\033[H");
        
        cmd_status(socket_path);
        printf("\n");
        cmd_top(socket_path);
        
        sleep(interval);
    }
    
    return 0;
}

int cmd_raw(const char *socket_path) {
    char *response = client_get_snapshot(socket_path);
    if (!response) {
        fprintf(stderr, "Error: Cannot connect to daemon at %s\n", socket_path);
        return 1;
    }
    
    printf("%s\n", response);
    return 0;
}
