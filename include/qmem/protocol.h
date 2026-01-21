/*
 * qmem/protocol.h - IPC protocol definitions
 */
#ifndef QMEM_PROTOCOL_H
#define QMEM_PROTOCOL_H

#include <stdint.h>

/* IPC socket default path */
#define QMEM_SOCKET_PATH "/run/qmem.sock"

/* Maximum message size */
#define QMEM_MSG_MAX_SIZE (256 * 1024)

/* Protocol version */
#define QMEM_PROTOCOL_VERSION 1

/* Request types */
typedef enum {
    QMEM_REQ_STATUS = 1,      /* Get current status */
    QMEM_REQ_SNAPSHOT = 2,    /* Get full snapshot */
    QMEM_REQ_HISTORY = 3,     /* Get historical data */
    QMEM_REQ_CONFIG = 4,      /* Get/set config */
    QMEM_REQ_SUBSCRIBE = 5,   /* Subscribe to updates */
    QMEM_REQ_SERVICES = 6,    /* List services */
    QMEM_REQ_SHUTDOWN = 99,   /* Shutdown daemon */
} qmem_req_type_t;

/* Response status */
typedef enum {
    QMEM_RESP_OK = 0,
    QMEM_RESP_ERROR = 1,
    QMEM_RESP_NOTFOUND = 2,
    QMEM_RESP_DENIED = 3,
} qmem_resp_status_t;

/* Message header (little-endian) */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 0x514D454D "QMEM" */
    uint16_t version;         /* Protocol version */
    uint16_t type;            /* Request/response type */
    uint32_t length;          /* Payload length */
    uint32_t seq;             /* Sequence number */
} qmem_msg_header_t;

#define QMEM_MSG_MAGIC 0x514D454D

/* Helper to initialize header */
static inline void qmem_msg_header_init(qmem_msg_header_t *h, uint16_t type, uint32_t len) {
    h->magic = QMEM_MSG_MAGIC;
    h->version = QMEM_PROTOCOL_VERSION;
    h->type = type;
    h->length = len;
    h->seq = 0;
}

#endif /* QMEM_PROTOCOL_H */
