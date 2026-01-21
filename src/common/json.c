/*
 * json.c - Lightweight JSON builder implementation
 */
#include "json.h"
#include <stdio.h>
#include <string.h>

static void json_write(json_builder_t *j, const char *str, size_t len) {
    if (j->error) return;
    
    if (j->pos + len >= j->size) {
        j->error = true;
        return;
    }
    
    memcpy(j->buf + j->pos, str, len);
    j->pos += len;
    j->buf[j->pos] = '\0';
}

static void json_write_str(json_builder_t *j, const char *str) {
    json_write(j, str, strlen(str));
}

static void json_comma_if_needed(json_builder_t *j) {
    if (j->needs_comma) {
        json_write(j, ",", 1);
    }
    j->needs_comma = false;
}

void json_init(json_builder_t *j, char *buf, size_t size) {
    j->buf = buf;
    j->size = size;
    j->pos = 0;
    j->depth = 0;
    j->needs_comma = false;
    j->error = false;
    
    if (size > 0) {
        buf[0] = '\0';
    }
}

void json_object_start(json_builder_t *j) {
    json_comma_if_needed(j);
    json_write(j, "{", 1);
    j->depth++;
    j->needs_comma = false;
}

void json_object_end(json_builder_t *j) {
    json_write(j, "}", 1);
    j->depth--;
    j->needs_comma = true;
}

void json_array_start(json_builder_t *j) {
    json_comma_if_needed(j);
    json_write(j, "[", 1);
    j->depth++;
    j->needs_comma = false;
}

void json_array_end(json_builder_t *j) {
    json_write(j, "]", 1);
    j->depth--;
    j->needs_comma = true;
}

void json_key(json_builder_t *j, const char *key) {
    json_comma_if_needed(j);
    json_write(j, "\"", 1);
    
    /* Escape key */
    for (const char *p = key; *p; p++) {
        switch (*p) {
            case '"':  json_write(j, "\\\"", 2); break;
            case '\\': json_write(j, "\\\\", 2); break;
            case '\n': json_write(j, "\\n", 2); break;
            case '\r': json_write(j, "\\r", 2); break;
            case '\t': json_write(j, "\\t", 2); break;
            default:   json_write(j, p, 1); break;
        }
    }
    
    json_write(j, "\":", 2);
}

void json_string(json_builder_t *j, const char *value) {
    json_comma_if_needed(j);
    
    if (!value) {
        json_write_str(j, "null");
        j->needs_comma = true;
        return;
    }
    
    json_write(j, "\"", 1);
    
    /* Escape value */
    for (const char *p = value; *p; p++) {
        switch (*p) {
            case '"':  json_write(j, "\\\"", 2); break;
            case '\\': json_write(j, "\\\\", 2); break;
            case '\n': json_write(j, "\\n", 2); break;
            case '\r': json_write(j, "\\r", 2); break;
            case '\t': json_write(j, "\\t", 2); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
                    json_write_str(j, esc);
                } else {
                    json_write(j, p, 1);
                }
                break;
        }
    }
    
    json_write(j, "\"", 1);
    j->needs_comma = true;
}

void json_int(json_builder_t *j, int64_t value) {
    json_comma_if_needed(j);
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%ld", (long)value);
    json_write(j, buf, len);
    j->needs_comma = true;
}

void json_uint(json_builder_t *j, uint64_t value) {
    json_comma_if_needed(j);
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
    json_write(j, buf, len);
    j->needs_comma = true;
}

void json_double(json_builder_t *j, double value) {
    json_comma_if_needed(j);
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%.6g", value);
    json_write(j, buf, len);
    j->needs_comma = true;
}

void json_bool(json_builder_t *j, bool value) {
    json_comma_if_needed(j);
    json_write_str(j, value ? "true" : "false");
    j->needs_comma = true;
}

void json_null(json_builder_t *j) {
    json_comma_if_needed(j);
    json_write_str(j, "null");
    j->needs_comma = true;
}

void json_kv_string(json_builder_t *j, const char *key, const char *value) {
    json_key(j, key);
    j->needs_comma = false;
    json_string(j, value);
}

void json_kv_int(json_builder_t *j, const char *key, int64_t value) {
    json_key(j, key);
    j->needs_comma = false;
    json_int(j, value);
}

void json_kv_uint(json_builder_t *j, const char *key, uint64_t value) {
    json_key(j, key);
    j->needs_comma = false;
    json_uint(j, value);
}

void json_kv_double(json_builder_t *j, const char *key, double value) {
    json_key(j, key);
    j->needs_comma = false;
    json_double(j, value);
}

void json_kv_bool(json_builder_t *j, const char *key, bool value) {
    json_key(j, key);
    j->needs_comma = false;
    json_bool(j, value);
}

size_t json_length(json_builder_t *j) {
    return j->pos;
}

bool json_error(json_builder_t *j) {
    return j->error;
}
