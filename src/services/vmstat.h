/*
 * vmstat.h - /proc/vmstat monitor
 */
#ifndef QMEM_VMSTAT_H
#define QMEM_VMSTAT_H

#include "service.h"

extern qmem_service_t vmstat_service;

/* vmstat data */
typedef struct {
    int64_t nr_slab_unreclaimable;
    int64_t nr_slab_reclaimable;
    int64_t nr_vmalloc;
    int64_t nr_kernel_stack;
    int64_t nr_page_table_pages;
    int64_t nr_dirty;
    int64_t nr_writeback;
} vmstat_data_t;

const vmstat_data_t *vmstat_get_current(void);

#endif /* QMEM_VMSTAT_H */
