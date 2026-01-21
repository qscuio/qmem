/*
 * commands.h - CLI command handlers
 */
#ifndef QMEM_COMMANDS_H
#define QMEM_COMMANDS_H

/* Execute status command
 * target: specific service to show, or NULL for summary */
int cmd_status(const char *socket_path, const char *target);

/* Execute top command (process list) */
int cmd_top(const char *socket_path);

/* Execute slab command */
int cmd_slab(const char *socket_path);

/* Execute watch command (continuous monitoring) */
int cmd_watch(const char *socket_path, int interval, const char *target);

/* Execute raw command (dump JSON) */
int cmd_raw(const char *socket_path);

/* List active services */
int cmd_services(const char *socket_path);

/* List active sockets */
int cmd_sockets(const char *socket_path);

#endif /* QMEM_COMMANDS_H */
