/*
 * proc_utils.h - /proc filesystem utilities
 */
#ifndef QMEM_PROC_UTILS_H
#define QMEM_PROC_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

/*
 * Read entire file into buffer
 * Returns bytes read, or -1 on error
 */
ssize_t proc_read_file(const char *path, char *buf, size_t size);

/*
 * Read a specific field from /proc/<pid>/status
 * Returns value in KB, or -1 on error
 */
int64_t proc_read_status_kb(pid_t pid, const char *field);

/*
 * Read /proc/<pid>/cmdline
 * Returns length of command, or -1 on error
 */
int proc_read_cmdline(pid_t pid, char *buf, size_t size);

/*
 * Read /proc/<pid>/comm
 */
int proc_read_comm(pid_t pid, char *buf, size_t size);

/*
 * Callback for iterating /proc PIDs
 */
typedef bool (*proc_pid_callback_t)(pid_t pid, void *userdata);

/*
 * Iterate all numeric /proc entries (process PIDs)
 * Callback returns false to stop iteration
 */
int proc_iterate_pids(proc_pid_callback_t callback, void *userdata);

/*
 * Check if process exists
 */
bool proc_pid_exists(pid_t pid);

/*
 * Parse key-value pair from /proc file line
 * Format: "Key:  value kB"
 * Returns value (without unit), key is stored in key_buf
 */
int64_t proc_parse_kv_kb(const char *line, char *key_buf, size_t key_size);

#endif /* QMEM_PROC_UTILS_H */
