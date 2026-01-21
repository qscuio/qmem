/*
 * ipc_server.c - Unix socket IPC server implementation
 */
#include "ipc_server.h"
#include "common/log.h"
#include "common/json.h"
#include <qmem/protocol.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

static int g_server_fd = -1;
static pthread_t g_server_thread;
static volatile int g_running = 0;
static char g_socket_path[256];

static ipc_snapshot_callback_t g_snapshot_cb = NULL;
static ipc_history_callback_t g_history_cb = NULL;

void ipc_set_snapshot_callback(ipc_snapshot_callback_t cb) {
    g_snapshot_cb = cb;
}

void ipc_set_history_callback(ipc_history_callback_t cb) {
    g_history_cb = cb;
}

static void handle_client(int client_fd) {
    qmem_msg_header_t header;
    
    /* Read header */
    ssize_t n = recv(client_fd, &header, sizeof(header), MSG_WAITALL);
    if (n != sizeof(header)) {
        log_debug("Failed to read IPC header");
        return;
    }
    
    /* Validate magic */
    if (header.magic != QMEM_MSG_MAGIC) {
        log_warn("Invalid IPC magic: 0x%x", header.magic);
        return;
    }
    
    /* Handle request */
    char response[QMEM_MSG_MAX_SIZE];
    json_builder_t json;
    json_init(&json, response + sizeof(qmem_msg_header_t), 
              sizeof(response) - sizeof(qmem_msg_header_t));
    
    switch (header.type) {
        case QMEM_REQ_STATUS:
        case QMEM_REQ_SNAPSHOT:
            if (g_snapshot_cb) {
                const char *snapshot = g_snapshot_cb();
                if (snapshot) {
                    size_t len = strlen(snapshot);
                    if (len < sizeof(response) - sizeof(qmem_msg_header_t)) {
                        memcpy(response + sizeof(qmem_msg_header_t), snapshot, len);
                        json.pos = len;
                    }
                }
            }
            break;
            
        case QMEM_REQ_HISTORY:
            if (g_history_cb) {
                /* Read count from payload if present */
                int count = 10;
                if (header.length >= sizeof(int)) {
                    recv(client_fd, &count, sizeof(int), MSG_WAITALL);
                }
                const char *history = g_history_cb(count);
                if (history) {
                    size_t len = strlen(history);
                    if (len < sizeof(response) - sizeof(qmem_msg_header_t)) {
                        memcpy(response + sizeof(qmem_msg_header_t), history, len);
                        json.pos = len;
                    }
                }
            }
            break;
            
        case QMEM_REQ_SERVICES:
            json_object_start(&json);
            json_kv_string(&json, "status", "ok");
            json_object_end(&json);
            break;
            
        default:
            json_object_start(&json);
            json_kv_string(&json, "error", "unknown request");
            json_object_end(&json);
            break;
    }
    
    /* Send response */
    qmem_msg_header_t *resp_header = (qmem_msg_header_t *)response;
    qmem_msg_header_init(resp_header, header.type, json_length(&json));
    resp_header->seq = header.seq;
    
    send(client_fd, response, sizeof(qmem_msg_header_t) + json_length(&json), 0);
}

static void *server_thread(void *arg) {
    (void)arg;
    
    struct pollfd pfd;
    pfd.fd = g_server_fd;
    pfd.events = POLLIN;
    
    log_info("IPC server started on %s", g_socket_path);
    
    while (g_running) {
        int ret = poll(&pfd, 1, 1000);  /* 1 second timeout */
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            log_error("poll() failed: %s", strerror(errno));
            break;
        }
        
        if (ret == 0) continue;  /* Timeout */
        
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
    
    log_info("IPC server stopped");
    return NULL;
}

int ipc_server_start(const qmem_config_t *cfg) {
    strncpy(g_socket_path, cfg->socket_path, sizeof(g_socket_path) - 1);
    
    /* Remove existing socket */
    unlink(g_socket_path);
    
    /* Create socket */
    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        log_error("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    /* Bind */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_socket_path, sizeof(addr.sun_path) - 1);
    
    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("Failed to bind socket: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }
    
    /* Listen */
    if (listen(g_server_fd, 5) < 0) {
        log_error("Failed to listen: %s", strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }
    
    /* Start thread */
    g_running = 1;
    if (pthread_create(&g_server_thread, NULL, server_thread, NULL) != 0) {
        log_error("Failed to create server thread");
        close(g_server_fd);
        g_server_fd = -1;
        g_running = 0;
        return -1;
    }
    
    return 0;
}

void ipc_server_stop(void) {
    if (!g_running) return;
    
    g_running = 0;
    
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }
    
    pthread_join(g_server_thread, NULL);
    
    unlink(g_socket_path);
}

int ipc_server_is_running(void) {
    return g_running;
}
