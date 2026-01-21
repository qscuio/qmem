/*
 * json.h - Lightweight JSON builder
 */
#ifndef QMEM_JSON_H
#define QMEM_JSON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* JSON builder context */
typedef struct {
    char *buf;
    size_t size;
    size_t pos;
    int depth;
    bool needs_comma;
    bool error;
} json_builder_t;

/* Initialize JSON builder with buffer */
void json_init(json_builder_t *j, char *buf, size_t size);

/* Start/end object */
void json_object_start(json_builder_t *j);
void json_object_end(json_builder_t *j);

/* Start/end array */
void json_array_start(json_builder_t *j);
void json_array_end(json_builder_t *j);

/* Add key (for objects) */
void json_key(json_builder_t *j, const char *key);

/* Add values */
void json_string(json_builder_t *j, const char *value);
void json_int(json_builder_t *j, int64_t value);
void json_uint(json_builder_t *j, uint64_t value);
void json_double(json_builder_t *j, double value);
void json_bool(json_builder_t *j, bool value);
void json_null(json_builder_t *j);

/* Convenience: key + value */
void json_kv_string(json_builder_t *j, const char *key, const char *value);
void json_kv_int(json_builder_t *j, const char *key, int64_t value);
void json_kv_uint(json_builder_t *j, const char *key, uint64_t value);
void json_kv_double(json_builder_t *j, const char *key, double value);
void json_kv_bool(json_builder_t *j, const char *key, bool value);

/* Get result length (excluding null terminator) */
size_t json_length(json_builder_t *j);

/* Check if there was an error (buffer overflow) */
bool json_error(json_builder_t *j);

#endif /* QMEM_JSON_H */
