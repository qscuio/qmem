/*
 * static_files.h - Embedded static file serving
 */
#ifndef QMEM_STATIC_FILES_H
#define QMEM_STATIC_FILES_H

#include "http_server.h"

/* Handler for static files */
void static_files_handler(const http_request_t *req, http_response_t *resp);

#endif /* QMEM_STATIC_FILES_H */
