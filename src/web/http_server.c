/*
 * http_server.c - Minimal embedded HTTP server implementation
 */
#include "http_server.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#define MAX_ROUTES 32
#define MAX_REQUEST_SIZE 65536
#define MAX_RESPONSE_SIZE (1024 * 1024)

typedef struct {
    char path[128];
    http_handler_t handler;
} route_t;

static int g_server_fd = -1;
static pthread_t g_server_thread;
static volatile int g_running = 0;
static route_t g_routes[MAX_ROUTES];
static int g_route_count = 0;
static http_handler_t g_default_handler = NULL;

void http_register_handler(const char *path, http_handler_t handler) {
    if (g_route_count >= MAX_ROUTES) {
        log_warn("Max routes reached");
        return;
    }
    
    route_t *r = &g_routes[g_route_count++];
    strncpy(r->path, path, sizeof(r->path) - 1);
    r->handler = handler;
}

void http_set_default_handler(http_handler_t handler) {
    g_default_handler = handler;
}

static http_handler_t find_handler(const char *path) {
    for (int i = 0; i < g_route_count; i++) {
        if (strcmp(g_routes[i].path, path) == 0) {
            return g_routes[i].handler;
        }
        /* Check prefix match for api/* patterns */
        size_t len = strlen(g_routes[i].path);
        if (len > 0 && g_routes[i].path[len-1] == '*') {
            if (strncmp(path, g_routes[i].path, len - 1) == 0) {
                return g_routes[i].handler;
            }
        }
    }
    return g_default_handler;
}

static int parse_request(const char *buf, size_t len, http_request_t *req) {
    static char method[16];
    static char path[1024];
    static char query[1024];
    
    memset(req, 0, sizeof(*req));
    
    /* Parse request line */
    const char *line_end = strstr(buf, "\r\n");
    if (!line_end) return -1;
    
    if (sscanf(buf, "%15s %1023s", method, path) != 2) {
        return -1;
    }
    
    req->method = method;
    
    /* Split path and query */
    char *q = strchr(path, '?');
    if (q) {
        *q = '\0';
        strncpy(query, q + 1, sizeof(query) - 1);
        req->query = query;
    } else {
        query[0] = '\0';
        req->query = query;
    }
    
    req->path = path;
    
    /* Find body (after \r\n\r\n) */
    const char *body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        req->body = body_start;
        req->body_len = len - (body_start - buf);
    }
    
    return 0;
}

static void send_response(int client_fd, const http_response_t *resp) {
    char header[1024];
    const char *status_text;
    
    switch (resp->status_code) {
        case 200: status_text = "OK"; break;
        case 201: status_text = "Created"; break;
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "Unknown"; break;
    }
    
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        resp->status_code, status_text,
        resp->content_type ? resp->content_type : "text/plain",
        resp->body_len);
    
    send(client_fd, header, header_len, 0);
    if (resp->body && resp->body_len > 0) {
        send(client_fd, resp->body, resp->body_len, 0);
    }
}

static void handle_client(int client_fd) {
    char buf[MAX_REQUEST_SIZE];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';
    
    http_request_t req;
    if (parse_request(buf, n, &req) < 0) {
        http_response_t resp = {400, "text/plain", "Bad Request", 11};
        send_response(client_fd, &resp);
        return;
    }
    
    log_debug("HTTP %s %s", req.method, req.path);
    
    http_handler_t handler = find_handler(req.path);
    if (!handler) {
        http_response_t resp = {404, "text/plain", "Not Found", 9};
        send_response(client_fd, &resp);
        return;
    }
    
    static char response_buf[MAX_RESPONSE_SIZE];
    http_response_t resp = {200, "application/json", response_buf, 0};
    
    handler(&req, &resp);
    send_response(client_fd, &resp);
}

static void *server_thread(void *arg) {
    (void)arg;
    
    struct pollfd pfd;
    pfd.fd = g_server_fd;
    pfd.events = POLLIN;
    
    log_info("HTTP server started");
    
    while (g_running) {
        int ret = poll(&pfd, 1, 1000);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            log_error("poll() failed: %s", strerror(errno));
            break;
        }
        
        if (ret == 0) continue;
        
        if (pfd.revents & POLLIN) {
            int client_fd = accept(g_server_fd, NULL, NULL);
            if (client_fd < 0) {
                log_warn("accept() failed: %s", strerror(errno));
                continue;
            }
            
            handle_client(client_fd);
            close(client_fd);
        }
    }
    
    log_info("HTTP server stopped");
    return NULL;
}

int http_server_start(const qmem_config_t *cfg) {
    if (!cfg->web_enabled) {
        log_info("Web server disabled");
        return 0;
    }
    
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        log_error("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    /* Allow address reuse */
    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg->web_port);
    inet_pton(AF_INET, cfg->web_listen, &addr.sin_addr);
    
    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("Failed to bind HTTP socket: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }
    
    if (listen(g_server_fd, 10) < 0) {
        log_error("Failed to listen: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }
    
    g_running = 1;
    if (pthread_create(&g_server_thread, NULL, server_thread, NULL) != 0) {
        log_error("Failed to create HTTP server thread");
        close(g_server_fd);
        g_server_fd = -1;
        g_running = 0;
        return -1;
    }
    
    log_info("HTTP server listening on %s:%d", cfg->web_listen, cfg->web_port);
    return 0;
}

void http_server_stop(void) {
    if (!g_running) return;
    
    g_running = 0;
    
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }
    
    pthread_join(g_server_thread, NULL);
}

int http_server_is_running(void) {
    return g_running;
}
