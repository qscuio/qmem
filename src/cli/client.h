/*
 * client.h - IPC client for CLI
 */
#ifndef QMEM_CLIENT_H
#define QMEM_CLIENT_H

#include <stddef.h>
#include <qmem/protocol.h>

/* Connect to daemon */
int client_connect(const char *socket_path);

/* Send request and receive response */
int client_request(int fd, qmem_req_type_t type, const void *data, size_t data_len,
                   char *response, size_t response_size);

/* Disconnect */
void client_disconnect(int fd);

/* Convenience functions */
char *client_get_status(const char *socket_path);
char *client_get_snapshot(const char *socket_path);
char *client_get_history(const char *socket_path, int count);

#endif /* QMEM_CLIENT_H */
