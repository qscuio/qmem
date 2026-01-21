/*
 * log.c - Logging subsystem implementation
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <stdbool.h>
#include <pthread.h>

static log_level_t g_log_level = QMEM_LOG_INFO;
static bool g_use_syslog = false;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

static const char *level_colors[] = {
    "\033[36m",  /* DEBUG: cyan */
    "\033[32m",  /* INFO: green */
    "\033[33m",  /* WARN: yellow */
    "\033[31m",  /* ERROR: red */
};

static int level_to_syslog[] = {
    LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR
};

void log_init(log_level_t level, bool use_syslog, const char *ident) {
    g_log_level = level;
    g_use_syslog = use_syslog;
    
    if (use_syslog) {
        openlog(ident ? ident : "qmemd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    }
}

void log_shutdown(void) {
    if (g_use_syslog) {
        closelog();
    }
}

void log_set_level(log_level_t level) {
    g_log_level = level;
}

static void log_write(log_level_t level, const char *fmt, va_list args) {
    /* Bounds check - ensure level is in valid range */
    int lvl = (int)level;
    if (lvl < 0 || lvl > 3) {
        lvl = QMEM_LOG_INFO;
    }
    
    if (lvl < (int)g_log_level) {
        return;
    }
    
    pthread_mutex_lock(&g_log_mutex);
    
    if (g_use_syslog) {
        vsyslog(level_to_syslog[lvl], fmt, args);
    } else {
        /* Get timestamp */
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
        
        /* Print with color */
        fprintf(stderr, "%s %s[%-5s]\033[0m ", 
                timebuf, level_colors[lvl], level_names[lvl]);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        fflush(stderr);
    }
    
    pthread_mutex_unlock(&g_log_mutex);
}

void log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(QMEM_LOG_DEBUG, fmt, args);
    va_end(args);
}

void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(QMEM_LOG_INFO, fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(QMEM_LOG_WARN, fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(QMEM_LOG_ERROR, fmt, args);
    va_end(args);
}

void log_msg(log_level_t level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(level, fmt, args);
    va_end(args);
}
