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
#define BOLD    "\033[1m"
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

static void print_json_section(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    
    if (!pos) {
        printf("Service '%s' not found or disabled in snapshot.\n", key);
        return;
    }
    
    printf("\n" CYAN "=== Status: %s ===" NC "\n\n", key);
    
    /* Find value start */
    pos = strchr(pos, ':');
    if (!pos) return;
    pos++;
    while (*pos && isspace(*pos)) pos++;
    
    /* Determine end */
    const char *end = NULL;
    if (*pos == '{') {
        int depth = 1;
        end = pos + 1;
        while (*end && depth > 0) {
            if (*end == '{') depth++;
            else if (*end == '}') depth--;
            end++;
        }
        if (*end) end++;
    } else if (*pos == '[') {
        int depth = 1;
        end = pos + 1;
        while (*end && depth > 0) {
            if (*end == '[') depth++;
            else if (*end == ']') depth--;
            end++;
        }
        if (*end) end++;
    } else if (*pos == '"') {
        end = pos + 1;
        while (*end && *end != '"') {
             if (*end == '\\') end++;
             end++;
        }
        if (*end) end++;
    } else {
        /* Number or boolean */
        end = pos;
        while (*end && (isalnum(*end) || *end == '.' || *end == '-')) end++;
    }
    
    if (end) {
        /* Print formatted JSON if possible using a simple heuristic tool? 
           For now just raw dump of the section */
        fwrite(pos, 1, end - pos, stdout);
        printf("\n");
    }
}

int cmd_memleak(const char *socket_path) {
    char *response = client_get_snapshot(socket_path);
    if (!response) {
        fprintf(stderr, "Error: Cannot connect to daemon at %s\n", socket_path);
        return 1;
    }

    printf("\n" CYAN "=== Memory Leak / Usage Report ===" NC "\n\n");
    const char *memleak = strstr(response, "\"memleak\":");
    if (!memleak) {
        printf("Service 'memleak' data not found.\n");
        free(response);
        return 0;
    }

    /* 0. Global Summary */
    const char *mem_sum = strstr(memleak, "\"memory_summary\":");
    if (mem_sum) {
        int64_t total = json_get_int(mem_sum, "total_kb");
        int64_t avail = json_get_int(mem_sum, "available_kb");
        int64_t free_kb = json_get_int(mem_sum, "free_kb");
        int64_t cached = json_get_int(mem_sum, "cached_kb");
        int64_t used = total - free_kb - cached; /* Rough used approximation */
        
        char s_total[32], s_used[32], s_avail[32];
        format_kb(s_total, sizeof(s_total), total);
        format_kb(s_used, sizeof(s_used), used);
        format_kb(s_avail, sizeof(s_avail), avail);
        
        printf(BOLD "System Memory:" NC " Total: %s  Used: %s  Available: %s\n\n", s_total, s_used, s_avail);
    }

    /* Helper macro for table headers */
#define HEADER_PROC "%-8s %-16s %-12s %-12s %-12s %-12s %-12s"
#define ROWS_PROC   "%-8ld %-16s %-12s %-12s %-12s %-12s %-12s"
#define HEADER_SLAB "%-24s %-12s %-12s %-12s"
#define ROWS_SLAB   "%-24s %-12s %-12s %-12ld"

    /* 1. Process Usage */
    const char *proc_usage = strstr(memleak, "\"process_usage\":");
    if (proc_usage) {
        printf(BOLD "Top Process Memory (Absolute)" NC "\n");
        printf(HEADER_PROC "\n", "PID", "Command", "Total RSS", "Delta", "Heap RSS", "Delta", "Heap Size");
        print_separator();
        
        const char *pos = strchr(proc_usage, '[');
        int count = 0;
        while (pos && (pos = strstr(pos, "{\"pid\":")) != NULL) {
             /* Parse fields */
             int64_t pid = json_get_int(pos, "pid");
             
             char cmd[64] = "unknown";
             const char *cmd_pos = strstr(pos, "\"cmd\":\"");
             if (cmd_pos) {
                 cmd_pos += 7;
                 const char *end = strchr(cmd_pos, '"');
                 if (end) {
                     int len = end - cmd_pos;
                     if (len > 63) len = 63;
                     snprintf(cmd, sizeof(cmd), "%.*s", len, cmd_pos);
                 }
             }

             int64_t rss = json_get_int(pos, "rss_kb");
             int64_t rss_delta = json_get_int(pos, "rss_delta_kb");
             int64_t heap = json_get_int(pos, "heap_rss_kb");
             int64_t heap_delta = json_get_int(pos, "heap_delta_kb");
             int64_t heap_size = json_get_int(pos, "heap_size_kb");

             char s_rss[32], s_rss_d[32], s_heap[32], s_heap_d[32], s_size[32];
             format_kb(s_rss, sizeof(s_rss), rss);
             format_delta(s_rss_d, sizeof(s_rss_d), rss_delta);
             format_kb(s_heap, sizeof(s_heap), heap);
             format_delta(s_heap_d, sizeof(s_heap_d), heap_delta);
             format_kb(s_size, sizeof(s_size), heap_size);

             printf(ROWS_PROC "\n", pid, cmd, s_rss, s_rss_d, s_heap, s_heap_d, s_size);
             
             pos++;
             count++;
        }
        if (count == 0) printf("(No process data available)\n");
        printf("\n");
    }

    /* 2. Kernel Usage */
    const char *kern_usage = strstr(memleak, "\"kernel_usage\":");
    if (kern_usage) {
        printf(BOLD "Top Slab Cache (Absolute)" NC "\n");
        printf(HEADER_SLAB "\n", "Cache Name", "Total Size", "Delta", "Objects");
        print_separator();
        
        const char *pos = strchr(kern_usage, '[');
        int count = 0;
        while (pos && (pos = strstr(pos, "{\"cache\":")) != NULL) {
             char cache[64] = "unknown";
             const char *c_pos = strstr(pos, "\"cache\":\"");
             if (c_pos) {
                 c_pos += 9;
                 const char *end = strchr(c_pos, '"');
                 if (end) {
                     int len = end - c_pos;
                     if (len > 63) len = 63;
                     snprintf(cache, sizeof(cache), "%.*s", len, c_pos);
                 }
             }

             int64_t total = json_get_int(pos, "total_bytes");
             int64_t delta = json_get_int(pos, "delta_bytes");
             int64_t objs = json_get_int(pos, "active_objs");

             char s_total[32], s_delta[32];
             /* format_kb handles KB. For bytes, we might want MB/KB auto logic but format_kb takes KB input. */
             /* Convert bytes to KB for format_kb */
             format_kb(s_total, sizeof(s_total), total / 1024);
             format_delta(s_delta, sizeof(s_delta), delta / 1024);

             printf(ROWS_SLAB "\n", cache, s_total, s_delta, objs);
             
             pos++;
             count++;
        }
        if (count == 0) printf("(No slab data available)\n");
        printf("\n");
    }

    /* 3. Leaks (Optional - only if non-empty?) 
       User output shows empty. We can show them to confirm "Clean". */
    const char *u_leaks = strstr(memleak, "\"user_leaks\":");
    if (u_leaks) {
        /* check if empty */
        const char *bracket = strchr(u_leaks, '[');
        if (bracket && strstr(bracket, "{\"pid\":")) {
             printf(RED BOLD "POSSIBLE USER LEAKS DETECTED!" NC "\n");
             /* Reuse process format roughly */
             /* Parse ... */
             /* For brevity, I'll just note them if user wants full leak report. 
                But user is "still no total usage". 
                I'll leave leak part simple for now or implement if needed. 
                User leaks are "Growth" so already covered by "Top Memory Growers" in `top` command?
                memleak algorithm is more advanced (heap scan).
                I'll print a summary line. */
             printf("(Run 'qmemctl watch memleak' for detailed leak list if any)\n\n");
        }
    }

    free(response);
    return 0;
}

int cmd_status(const char *socket_path, const char *target) {
    if (target) {
        /* Delegate to specific commands if available */
        if (strcmp(target, "sockets") == 0 || strcmp(target, "sockstat") == 0) return cmd_sockets(socket_path);
        if (strcmp(target, "top") == 0 || strcmp(target, "procmem") == 0) return cmd_top(socket_path);
        if (strcmp(target, "slab") == 0 || strcmp(target, "slabinfo") == 0) return cmd_slab(socket_path);
        if (strcmp(target, "memleak") == 0) return cmd_memleak(socket_path);
        if (strcmp(target, "list") == 0 || strcmp(target, "services") == 0) return cmd_services(socket_path);
    }

    char *response = client_get_snapshot(socket_path);
    if (!response) {
        fprintf(stderr, "Error: Cannot connect to daemon at %s\n", socket_path);
        return 1;
    }
    
    if (target) {
        /* Generic print for other sections */
        print_json_section(response, target);
        free(response);
        return 0;
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
        printf("%-10s %12s %12s %12s %12s\n", "Interface", "RX bytes", "RX delta", "TX bytes", "TX delta");
        
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
                int64_t rx_delta = json_get_int(pos, "rx_bytes_delta");
                int64_t tx_delta = json_get_int(pos, "tx_bytes_delta");
                
                char rx_buf[32], tx_buf[32], rxd_buf[32], txd_buf[32];
                format_kb(rx_buf, sizeof(rx_buf), rx / 1024);
                format_kb(tx_buf, sizeof(tx_buf), tx / 1024);
                format_kb(rxd_buf, sizeof(rxd_buf), rx_delta / 1024);
                format_kb(txd_buf, sizeof(txd_buf), tx_delta / 1024);
                
                printf("%-10s %12s %12s %12s %12s\n", name, rx_buf, rxd_buf, tx_buf, txd_buf);
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
            
            int64_t total_d = json_get_int(tcp, "total_delta");
            int64_t estab_d = json_get_int(tcp, "established_delta");
            int64_t tw_d = json_get_int(tcp, "time_wait_delta");
            
            char t_str[64], e_str[64], tw_str[64];
            snprintf(t_str, 64, "%ld", (long)total);
            if (total_d != 0) snprintf(t_str+strlen(t_str), 64-strlen(t_str), "%s%ld", total_d>0?"+":"", (long)total_d);
            
            snprintf(e_str, 64, "%ld", (long)estab);
            if (estab_d != 0) snprintf(e_str+strlen(e_str), 64-strlen(e_str), "%s%ld", estab_d>0?"+":"", (long)estab_d);

            snprintf(tw_str, 64, "%ld", (long)tw);
            if (tw_d != 0) snprintf(tw_str+strlen(tw_str), 64-strlen(tw_str), "%s%ld", tw_d>0?"+":"", (long)tw_d);
            
            printf("TCP: %s (ESTAB:%s TIME_WAIT:%s LISTEN:%ld)  ",
                   t_str, e_str, tw_str, (long)listen);
        }
        int64_t udp = json_get_int(sockstat, "udp_total");
        int64_t unix_sock = json_get_int(sockstat, "unix_total");
        int64_t udp_d = json_get_int(sockstat, "udp_total_delta");
        int64_t unix_d = json_get_int(sockstat, "unix_total_delta");
        
        char u_str[64], ux_str[64];
        snprintf(u_str, 64, "%ld", (long)udp);
        if (udp_d != 0) snprintf(u_str+strlen(u_str), 64-strlen(u_str), "%s%ld", udp_d>0?"+":"", (long)udp_d);
        
        snprintf(ux_str, 64, "%ld", (long)unix_sock);
        if (unix_d != 0) snprintf(ux_str+strlen(ux_str), 64-strlen(ux_str), "%s%ld", unix_d>0?"+":"", (long)unix_d);
        
        printf("UDP: %s  Unix: %s\n", u_str, ux_str);
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
        
        const char *events = strstr(procevent, "\"recent_events\":");
        if (events) {
            printf("Recent Events:\n");
            const char *pos = strchr(events, '[');
            int count = 0;
            while (pos && (pos = strstr(pos, "{\"pid\":")) != NULL && count < 5) {
                int64_t pid = json_get_int(pos, "pid");
                
                /* Extract cmd */
                char cmd[64] = "unknown";
                const char *cmd_pos = strstr(pos, "\"cmd\":\"");
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
                
                /* Extract type */
                char type[16] = "unknown";
                const char *type_pos = strstr(pos, "\"type\":\"");
                if (type_pos) {
                    type_pos += 8;
                    const char *end = strchr(type_pos, '"');
                    if (end) {
                        size_t len = end - type_pos;
                        if (len > 15) len = 15;
                        strncpy(type, type_pos, len);
                        type[len] = '\0';
                    }
                }
                
                printf("  %-6s PID:%-6ld %s\n", type, (long)pid, cmd);
                
                pos++;
                count++;
            }
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
    

    
    /* Find top_rss section */
    const char *top_rss = strstr(procmem, "\"top_rss\":");
    if (top_rss) {
        printf("\n" CYAN "=== Top Memory Usage (Absolute) ===" NC "\n");
        printf("%-8s %-12s %-12s %s\n", "PID", "RSS", "Data", "Command");
        
        const char *pos = strchr(top_rss, '[');
        if (pos) {
            while ((pos = strstr(pos, "{\"pid\":")) != NULL) {
                int64_t pid = json_get_int(pos, "pid");
                int64_t rss = json_get_int(pos, "rss_kb");
                int64_t data = json_get_int(pos, "data_kb");
                
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
                
                char rss_buf[32], data_buf[32];
                format_kb(rss_buf, sizeof(rss_buf), rss);
                format_kb(data_buf, sizeof(data_buf), data);
                
                printf("%-8ld %-12s %-12s %.40s\n", (long)pid, rss_buf, data_buf, cmd);
                
                pos++;
            }
        }
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

int cmd_watch(const char *socket_path, int interval, const char *target) {
    printf("Watching memory changes (Ctrl+C to stop)...\n");
    printf("Interval: %d seconds\n\n", interval);
    
    while (1) {
        /* Clear screen */
        printf("\033[2J\033[H");
        
        cmd_status(socket_path, target);
        
        if (!target) {
            printf("\n");
            cmd_top(socket_path);
        }
        
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

int cmd_services(const char *socket_path) {
    char *response = client_get_snapshot(socket_path);
    if (!response) {
        fprintf(stderr, "Error: Cannot connect to daemon at %s\n", socket_path);
        return 1;
    }
    
    printf("\n" CYAN "=== Active Services ===" NC "\n");
    
    const char *services[] = {
        "meminfo", "System Memory",
        "slabinfo", "Slab Cache",
        "procmem", "Process Memory",
        "heapmon", "Heap Analysis",
        "vmstat", "VM Statistics",
        "cpuload", "CPU Load",
        "netstat", "Network Stats",
        "procstat", "Process States",
        "sockstat", "Socket Stats",
        "procevent", "Process Events",
        NULL
    };
    
    for (int i = 0; services[i]; i += 2) {
        if (json_find_key(response, services[i])) {
            printf(GREEN "  %-12s" NC " %s\n", services[i], services[i+1]);
        }
    }
    printf("\n");
    
    free(response);
    return 0;
}

static const char *get_sock_state_name(int state) {
    switch (state) {
        case 1:  return "ESTAB";
        case 2:  return "SYN_SENT";
        case 3:  return "SYN_RECV";
        case 4:  return "FIN_WAIT1";
        case 5:  return "FIN_WAIT2";
        case 6:  return "TIME_WAIT";
        case 7:  return "CLOSE";
        case 8:  return "CLOSE_WAIT";
        case 9:  return "LAST_ACK";
        case 10: return "LISTEN";
        case 11: return "CLOSING";
        default: return "UNKNOWN";
    }
}

int cmd_sockets(const char *socket_path) {
    char *response = client_get_snapshot(socket_path);
    if (!response) {
        fprintf(stderr, "Error: Cannot connect to daemon at %s\n", socket_path);
        return 1;
    }
    
    printf("\n" CYAN "=== Active Sockets ===" NC "\n\n");
    
    const char *sockstat = strstr(response, "\"sockstat\":");
    if (!sockstat) {
        printf("No socket data available.\n");
        free(response);
        return 0;
    }
    
    const char *sockets = strstr(sockstat, "\"sockets\":");
    if (!sockets) {
        printf("No detailed socket info available.\n");
        free(response);
        return 0;
    }
    
    printf("%-8s %-16s %-22s %-22s %-10s %-8s %-8s\n", 
           "PID", "Command", "Local Address", "Remote Address", "State", "TX_Q", "RX_Q");
    print_separator();
    
    const char *pos = strchr(sockets, '[');
    if (!pos) {
        free(response);
        return 0;
    }
    
    while ((pos = strstr(pos, "{\"local\":")) != NULL) {
        /* Extract fields */
        
        char local[64] = "";
        const char *l_pos = strstr(pos, "\"local\":\"");
        if (l_pos) {
            l_pos += 9;
            const char *end = strchr(l_pos, '"');
            if (end) {
                int len = end - l_pos;
                if (len > 63) len = 63;
                strncpy(local, l_pos, len);
                local[len] = '\0';
            }
        }
        
        char remote[64] = "";
        const char *r_pos = strstr(pos, "\"remote\":\"");
        if (r_pos) {
            r_pos += 10;
            const char *end = strchr(r_pos, '"');
            if (end) {
                int len = end - r_pos;
                if (len > 63) len = 63;
                strncpy(remote, r_pos, len);
                remote[len] = '\0';
            }
        }
        
        int64_t state = json_get_int(pos, "state");
        int64_t tx_q = json_get_int(pos, "tx_q");
        int64_t rx_q = json_get_int(pos, "rx_q");
        int64_t pid = json_get_int(pos, "pid");
        
        char cmd[32] = "-";
        const char *c_pos = strstr(pos, "\"cmd\":\"");
        if (c_pos) {
            c_pos += 7;
            const char *end = strchr(c_pos, '"');
            if (end) {
                int len = end - c_pos;
                if (len > 31) len = 31;
                strncpy(cmd, c_pos, len);
                cmd[len] = '\0';
            }
        }
        
        const char *state_str = get_sock_state_name((int)state);
        
        printf("%-8ld %-16s %-22s %-22s %-10s %-8ld %-8ld\n",
               (long)pid, cmd, local, remote, state_str, (long)tx_q, (long)rx_q);
        
        pos++;
    }
    
    printf("\n");
    free(response);
    return 0;
}
