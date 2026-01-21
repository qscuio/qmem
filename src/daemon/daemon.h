/*
 * daemon.h - Daemon lifecycle management
 */
#ifndef QMEM_DAEMON_H
#define QMEM_DAEMON_H

#include "config.h"

/* Initialize daemon */
int daemon_init(const qmem_config_t *cfg);

/* Run main loop (blocks until shutdown) */
int daemon_run(void);

/* Request shutdown */
void daemon_shutdown(void);

/* Reload configuration (SIGHUP) */
void daemon_reload(void);

/* Check if daemon is running */
int daemon_is_running(void);

#endif /* QMEM_DAEMON_H */
