/*
 * ringbuffer.c - Circular buffer for snapshot history
 */
#include "ringbuffer.h"
#include <stdlib.h>
#include <string.h>

ringbuf_t *ringbuf_create(int capacity) {
    ringbuf_t *rb = malloc(sizeof(ringbuf_t));
    if (!rb) return NULL;
    
    rb->entries = calloc(capacity, sizeof(ringbuf_entry_t));
    if (!rb->entries) {
        free(rb);
        return NULL;
    }
    
    rb->capacity = capacity;
    rb->head = 0;
    rb->count = 0;
    
    return rb;
}

void ringbuf_destroy(ringbuf_t *rb) {
    if (!rb) return;
    
    ringbuf_clear(rb);
    free(rb->entries);
    free(rb);
}

int ringbuf_push(ringbuf_t *rb, const char *data, size_t size) {
    if (!rb || !data) return -1;
    
    /* Free old entry if overwriting */
    ringbuf_entry_t *entry = &rb->entries[rb->head];
    if (entry->data) {
        free(entry->data);
    }
    
    /* Copy data */
    entry->data = malloc(size + 1);
    if (!entry->data) return -1;
    
    memcpy(entry->data, data, size);
    entry->data[size] = '\0';
    entry->size = size;
    entry->timestamp = time(NULL);
    
    /* Advance head */
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->count < rb->capacity) {
        rb->count++;
    }
    
    return 0;
}

const ringbuf_entry_t *ringbuf_get(ringbuf_t *rb, int index) {
    if (!rb || index < 0 || index >= rb->count) return NULL;
    
    /* Calculate actual position (oldest is at head - count) */
    int pos = (rb->head - rb->count + index + rb->capacity) % rb->capacity;
    return &rb->entries[pos];
}

const ringbuf_entry_t *ringbuf_get_recent(ringbuf_t *rb, int index) {
    if (!rb || index < 0 || index >= rb->count) return NULL;
    
    /* Most recent is at head - 1 */
    int pos = (rb->head - 1 - index + rb->capacity) % rb->capacity;
    return &rb->entries[pos];
}

int ringbuf_count(ringbuf_t *rb) {
    return rb ? rb->count : 0;
}

void ringbuf_clear(ringbuf_t *rb) {
    if (!rb) return;
    
    for (int i = 0; i < rb->capacity; i++) {
        if (rb->entries[i].data) {
            free(rb->entries[i].data);
            rb->entries[i].data = NULL;
        }
    }
    
    rb->head = 0;
    rb->count = 0;
}
