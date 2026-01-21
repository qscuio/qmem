/*
 * client.c - IPC client implementation
 */
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

int client_connect(const char *socket_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    
    return fd;
}

int client_request(int fd, qmem_req_type_t type, const void *data, size_t data_len,
                   char *response, size_t response_size) {
    /* Send header */
    qmem_msg_header_t header;
    qmem_msg_header_init(&header, type, data_len);
    
    if (send(fd, &header, sizeof(header), 0) != sizeof(header)) {
        return -1;
    }
    
    /* Send data if any */
    if (data && data_len > 0) {
        if (send(fd, data, data_len, 0) != (ssize_t)data_len) {
            return -1;
        }
    }
    
    /* Receive response header */
    qmem_msg_header_t resp_header;
    ssize_t n = recv(fd, &resp_header, sizeof(resp_header), MSG_WAITALL);
    if (n != sizeof(resp_header)) {
        return -1;
    }
    
    /* Validate */
    if (resp_header.magic != QMEM_MSG_MAGIC) {
        return -1;
    }
    
    /* Receive payload */
    size_t to_read = resp_header.length;
    if (to_read >= response_size) {
        to_read = response_size - 1;
    }
    
    if (to_read > 0) {
        n = recv(fd, response, to_read, MSG_WAITALL);
        if (n < 0) {
            return -1;
        }
        response[n] = '\0';
    } else {
        response[0] = '\0';
    }
    
    return (int)resp_header.length;
}

void client_disconnect(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

static char *do_request(const char *socket_path, qmem_req_type_t type, 
                       const void *data, size_t data_len) {
    int fd = client_connect(socket_path);
    if (fd < 0) {
        return NULL;
    }
    
    static char response[256 * 1024];
    int ret = client_request(fd, type, data, data_len, response, sizeof(response));
    client_disconnect(fd);
    
    if (ret < 0) {
        return NULL;
    }
    
    return response;
}

char *client_get_status(const char *socket_path) {
    return do_request(socket_path, QMEM_REQ_STATUS, NULL, 0);
}

char *client_get_snapshot(const char *socket_path) {
    return do_request(socket_path, QMEM_REQ_SNAPSHOT, NULL, 0);
}

char *client_get_history(const char *socket_path, int count) {
    return do_request(socket_path, QMEM_REQ_HISTORY, &count, sizeof(count));
}
