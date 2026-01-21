/*
 * format.h - Human-readable formatting utilities
 */
#ifndef QMEM_FORMAT_H
#define QMEM_FORMAT_H

#include <stdint.h>
#include <stddef.h>

/*
 * Format bytes as human-readable string (KB, MB, GB)
 * Returns pointer to static buffer (thread-local)
 */
const char *format_bytes(int64_t bytes);

/*
 * Format kilobytes as human-readable string
 */
const char *format_kb(int64_t kb);

/*
 * Format delta with +/- prefix
 * Returns pointer to static buffer (thread-local)
 */
const char *format_delta_kb(int64_t delta_kb);

/*
 * Format delta bytes
 */
const char *format_delta_bytes(int64_t delta_bytes);

/*
 * Format percentage (0-100)
 */
const char *format_percent(double percent);

/*
 * Format a value into the provided buffer
 */
int format_kb_buf(char *buf, size_t size, int64_t kb);
int format_bytes_buf(char *buf, size_t size, int64_t bytes);

#endif /* QMEM_FORMAT_H */
