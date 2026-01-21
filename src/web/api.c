/*
 * api.c - REST API handlers implementation
 */
#include "api.h"
#include "static_files.h"
#include "common/log.h"
#include <string.h>

static api_snapshot_callback_t g_snapshot_cb = NULL;

void api_set_snapshot_callback(api_snapshot_callback_t cb) {
    g_snapshot_cb = cb;
}

static void handle_api_status(const http_request_t *req, http_response_t *resp) {
    (void)req;
    
    if (g_snapshot_cb) {
        const char *snapshot = g_snapshot_cb();
        if (snapshot) {
            resp->body = snapshot;
            resp->body_len = strlen(snapshot);
            resp->content_type = "application/json";
            resp->status_code = 200;
            return;
        }
    }
    
    resp->body = "{\"error\":\"No data available\"}";
    resp->body_len = strlen(resp->body);
    resp->status_code = 503;
}

static void handle_api_health(const http_request_t *req, http_response_t *resp) {
    (void)req;
    
    resp->body = "{\"status\":\"ok\"}";
    resp->body_len = strlen(resp->body);
    resp->content_type = "application/json";
    resp->status_code = 200;
}

void api_init(void) {
    http_register_handler("/api/status", handle_api_status);
    http_register_handler("/api/snapshot", handle_api_status);
    http_register_handler("/api/health", handle_api_health);
    
    /* Set static file handler as default */
    http_set_default_handler(static_files_handler);
    
    log_info("API routes registered");
}
