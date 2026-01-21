/*
 * proc_utils.c - /proc filesystem utilities
 */
#include "proc_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

ssize_t proc_read_file(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    
    ssize_t total = 0;
    while ((size_t)total < size - 1) {
        ssize_t n = read(fd, buf + total, size - 1 - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (n == 0) break;
        total += n;
    }
    
    buf[total] = '\0';
    close(fd);
    return total;
}

int64_t proc_read_status_kb(pid_t pid, const char *field) {
    char path[64];
    char buf[4096];
    
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    
    if (proc_read_file(path, buf, sizeof(buf)) < 0) {
        return -1;
    }
    
    /* Search for field */
    size_t field_len = strlen(field);
    char *line = buf;
    
    while (*line) {
        if (strncmp(line, field, field_len) == 0 && line[field_len] == ':') {
            /* Found field, parse value */
            char *val = line + field_len + 1;
            while (*val && isspace(*val)) val++;
            return strtoll(val, NULL, 10);
        }
        
        /* Next line */
        line = strchr(line, '\n');
        if (!line) break;
        line++;
    }
    
    return -1;
}

int proc_read_cmdline(pid_t pid, char *buf, size_t size) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    
    ssize_t n = proc_read_file(path, buf, size);
    if (n < 0) {
        return -1;
    }
    
    /* Replace null bytes with spaces */
    for (ssize_t i = 0; i < n - 1; i++) {
        if (buf[i] == '\0') {
            buf[i] = ' ';
        }
    }
    
    /* Trim trailing whitespace */
    while (n > 0 && isspace(buf[n-1])) {
        buf[--n] = '\0';
    }
    
    return (int)n;
}

int proc_read_comm(pid_t pid, char *buf, size_t size) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    
    ssize_t n = proc_read_file(path, buf, size);
    if (n < 0) {
        return -1;
    }
    
    /* Trim trailing newline */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) {
        buf[--n] = '\0';
    }
    
    return (int)n;
}

int proc_iterate_pids(proc_pid_callback_t callback, void *userdata) {
    DIR *dir = opendir("/proc");
    if (!dir) {
        return -1;
    }
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        /* Check if entry is numeric (PID) */
        char *name = entry->d_name;
        bool is_pid = true;
        
        for (char *p = name; *p; p++) {
            if (!isdigit(*p)) {
                is_pid = false;
                break;
            }
        }
        
        if (is_pid) {
            pid_t pid = (pid_t)atoi(name);
            count++;
            
            if (!callback(pid, userdata)) {
                break;
            }
        }
    }
    
    closedir(dir);
    return count;
}

bool proc_pid_exists(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d", pid);
    return access(path, F_OK) == 0;
}

int64_t proc_parse_kv_kb(const char *line, char *key_buf, size_t key_size) {
    const char *colon = strchr(line, ':');
    if (!colon) {
        return -1;
    }
    
    /* Extract key */
    size_t key_len = colon - line;
    if (key_len >= key_size) {
        key_len = key_size - 1;
    }
    
    /* Skip leading whitespace in key */
    while (key_len > 0 && isspace(line[key_len-1])) {
        key_len--;
    }
    
    memcpy(key_buf, line, key_len);
    key_buf[key_len] = '\0';
    
    /* Trim key */
    char *k = key_buf;
    while (*k && isspace(*k)) k++;
    if (k != key_buf) {
        memmove(key_buf, k, strlen(k) + 1);
    }
    
    /* Parse value */
    const char *val = colon + 1;
    while (*val && isspace(*val)) val++;
    
    return strtoll(val, NULL, 10);
}
