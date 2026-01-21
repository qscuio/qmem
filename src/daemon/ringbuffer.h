/*
 * ringbuffer.h - Circular buffer for snapshot history
 */
#ifndef QMEM_RINGBUFFER_H
#define QMEM_RINGBUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Snapshot entry */
typedef struct {
    time_t timestamp;
    char *data;
    size_t size;
} ringbuf_entry_t;

/* Ring buffer */
typedef struct {
    ringbuf_entry_t *entries;
    int capacity;
    int head;       /* Next write position */
    int count;      /* Number of valid entries */
} ringbuf_t;

/* Create ring buffer */
ringbuf_t *ringbuf_create(int capacity);

/* Destroy ring buffer */
void ringbuf_destroy(ringbuf_t *rb);

/* Add entry (copies data) */
int ringbuf_push(ringbuf_t *rb, const char *data, size_t size);

/* Get entry by index (0 = oldest) */
const ringbuf_entry_t *ringbuf_get(ringbuf_t *rb, int index);

/* Get recent entry (0 = newest) */
const ringbuf_entry_t *ringbuf_get_recent(ringbuf_t *rb, int index);

/* Get number of entries */
int ringbuf_count(ringbuf_t *rb);

/* Clear all entries */
void ringbuf_clear(ringbuf_t *rb);

#endif /* QMEM_RINGBUFFER_H */
