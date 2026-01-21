/*
 * slabinfo.h - /proc/slabinfo monitor
 */
#ifndef QMEM_SLABINFO_H
#define QMEM_SLABINFO_H

#include "service.h"

extern qmem_service_t slabinfo_service;

/* Slab cache entry */
typedef struct {
    char name[64];
    int64_t size_bytes;
    int64_t delta_bytes;
    int32_t num_objs;
    int32_t obj_size;
} slab_entry_t;

/* Get top N growers/shrinkers */
int slabinfo_get_top_growers(slab_entry_t *entries, int max_entries);
int slabinfo_get_top_shrinkers(slab_entry_t *entries, int max_entries);

#endif /* QMEM_SLABINFO_H */
