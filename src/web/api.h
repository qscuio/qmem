/*
 * api.h - REST API handlers
 */
#ifndef QMEM_API_H
#define QMEM_API_H

#include "http_server.h"

/* Initialize API routes */
void api_init(void);

/* Set callback to get current snapshot */
typedef const char *(*api_snapshot_callback_t)(void);
void api_set_snapshot_callback(api_snapshot_callback_t cb);

#endif /* QMEM_API_H */
