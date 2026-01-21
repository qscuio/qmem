/*
 * meminfo.h - /proc/meminfo monitor
 */
#ifndef QMEM_MEMINFO_H
#define QMEM_MEMINFO_H

#include "service.h"

/* Access the meminfo service */
extern qmem_service_t meminfo_service;

/* Meminfo snapshot data (for direct access) */
typedef struct {
    int64_t mem_total_kb;
    int64_t mem_available_kb;
    int64_t mem_free_kb;
    int64_t buffers_kb;
    int64_t cached_kb;
    int64_t slab_kb;
    int64_t sreclaimable_kb;
    int64_t sunreclaim_kb;
    int64_t active_kb;
    int64_t inactive_kb;
    int64_t anon_pages_kb;
    int64_t vmalloc_used_kb;
    int64_t page_tables_kb;
    int64_t kernel_stack_kb;
    int64_t dirty_kb;
    int64_t mapped_kb;
    double usage_percent;
} meminfo_data_t;

/* Get current meminfo data */
const meminfo_data_t *meminfo_get_current(void);
const meminfo_data_t *meminfo_get_previous(void);

#endif /* QMEM_MEMINFO_H */
