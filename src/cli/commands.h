/*
 * commands.h - CLI command handlers
 */
#ifndef QMEM_COMMANDS_H
#define QMEM_COMMANDS_H

/* Execute status command */
int cmd_status(const char *socket_path);

/* Execute top command (process list) */
int cmd_top(const char *socket_path);

/* Execute slab command */
int cmd_slab(const char *socket_path);

/* Execute watch command (continuous monitoring) */
int cmd_watch(const char *socket_path, int interval);

/* Execute raw command (dump JSON) */
int cmd_raw(const char *socket_path);

#endif /* QMEM_COMMANDS_H */
