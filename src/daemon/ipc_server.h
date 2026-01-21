/*
 * ipc_server.h - Unix socket IPC server
 */
#ifndef QMEM_IPC_SERVER_H
#define QMEM_IPC_SERVER_H

#include "config.h"

/* Start IPC server (creates thread) */
int ipc_server_start(const qmem_config_t *cfg);

/* Stop IPC server */
void ipc_server_stop(void);

/* Check if server is running */
int ipc_server_is_running(void);

/* Set callback for getting current snapshot */
typedef const char *(*ipc_snapshot_callback_t)(void);
void ipc_set_snapshot_callback(ipc_snapshot_callback_t cb);

/* Set callback for getting history */
typedef const char *(*ipc_history_callback_t)(int count);
void ipc_set_history_callback(ipc_history_callback_t cb);

#endif /* QMEM_IPC_SERVER_H */
