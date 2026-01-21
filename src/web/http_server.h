/*
 * http_server.h - Minimal embedded HTTP server
 */
#ifndef QMEM_HTTP_SERVER_H
#define QMEM_HTTP_SERVER_H

#include <stddef.h>
#include "daemon/config.h"

/* HTTP request handler callback */
typedef struct {
    const char *method;
    const char *path;
    const char *query;
    const char *body;
    size_t body_len;
} http_request_t;

typedef struct {
    int status_code;
    const char *content_type;
    const char *body;
    size_t body_len;
} http_response_t;

typedef void (*http_handler_t)(const http_request_t *req, http_response_t *resp);

/* Start HTTP server (creates thread) */
int http_server_start(const qmem_config_t *cfg);

/* Stop HTTP server */
void http_server_stop(void);

/* Check if server is running */
int http_server_is_running(void);

/* Register route handler */
void http_register_handler(const char *path, http_handler_t handler);

/* Set default handler (for static files) */
void http_set_default_handler(http_handler_t handler);

#endif /* QMEM_HTTP_SERVER_H */
