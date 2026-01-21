/*
 * log.h - Logging subsystem
 */
#ifndef QMEM_LOG_H
#define QMEM_LOG_H

#include <stdarg.h>
#include <stdbool.h>

typedef enum {
    QMEM_LOG_DEBUG = 0,
    QMEM_LOG_INFO = 1,
    QMEM_LOG_WARN = 2,
    QMEM_LOG_ERROR = 3,
} log_level_t;

/* Convenience aliases */
#define LOG_LVL_DEBUG QMEM_LOG_DEBUG
#define LOG_LVL_INFO  QMEM_LOG_INFO
#define LOG_LVL_WARN  QMEM_LOG_WARN
#define LOG_LVL_ERROR QMEM_LOG_ERROR

/* Initialize logging */
void log_init(log_level_t level, bool use_syslog, const char *ident);

/* Shutdown logging */
void log_shutdown(void);

/* Set log level */
void log_set_level(log_level_t level);

/* Log functions */
void log_debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Log with explicit level */
void log_msg(log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif /* QMEM_LOG_H */
