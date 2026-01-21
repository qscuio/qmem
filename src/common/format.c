/*
 * format.c - Human-readable formatting utilities
 */
#include "format.h"
#include <stdio.h>
#include <stdlib.h>

/* Thread-local static buffers for formatting */
static __thread char g_format_buf[8][64];
static __thread int g_format_idx = 0;

static char *get_format_buf(void) {
    char *buf = g_format_buf[g_format_idx];
    g_format_idx = (g_format_idx + 1) % 8;
    return buf;
}

int format_kb_buf(char *buf, size_t size, int64_t kb) {
    if (kb >= 1048576) {  /* >= 1 GB */
        return snprintf(buf, size, "%.2f GB", (double)kb / 1048576.0);
    } else if (kb >= 1024) {  /* >= 1 MB */
        return snprintf(buf, size, "%.2f MB", (double)kb / 1024.0);
    } else {
        return snprintf(buf, size, "%ld KB", (long)kb);
    }
}

int format_bytes_buf(char *buf, size_t size, int64_t bytes) {
    return format_kb_buf(buf, size, bytes / 1024);
}

const char *format_kb(int64_t kb) {
    char *buf = get_format_buf();
    format_kb_buf(buf, 64, kb);
    return buf;
}

const char *format_bytes(int64_t bytes) {
    return format_kb(bytes / 1024);
}

const char *format_delta_kb(int64_t delta_kb) {
    char *buf = get_format_buf();
    char value_buf[32];
    
    int64_t abs_val = delta_kb < 0 ? -delta_kb : delta_kb;
    format_kb_buf(value_buf, sizeof(value_buf), abs_val);
    
    if (delta_kb > 0) {
        snprintf(buf, 64, "+%s", value_buf);
    } else if (delta_kb < 0) {
        snprintf(buf, 64, "-%s", value_buf);
    } else {
        snprintf(buf, 64, "0 KB");
    }
    
    return buf;
}

const char *format_delta_bytes(int64_t delta_bytes) {
    return format_delta_kb(delta_bytes / 1024);
}

const char *format_percent(double percent) {
    char *buf = get_format_buf();
    snprintf(buf, 64, "%.2f%%", percent);
    return buf;
}
