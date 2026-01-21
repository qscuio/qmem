/*
 * meminfo.c - /proc/meminfo monitor implementation
 */
#include "meminfo.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    meminfo_data_t current;
    meminfo_data_t previous;
    bool has_previous;
} meminfo_priv_t;

static meminfo_priv_t g_meminfo;

static int meminfo_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_meminfo, 0, sizeof(g_meminfo));
    svc->priv = &g_meminfo;
    
    log_debug("meminfo service initialized");
    return 0;
}

static int parse_meminfo(meminfo_data_t *data) {
    char buf[8192];
    
    if (proc_read_file("/proc/meminfo", buf, sizeof(buf)) < 0) {
        log_error("Failed to read /proc/meminfo");
        return -1;
    }
    
    memset(data, 0, sizeof(*data));
    
    char *line = buf;
    while (*line) {
        char key[64];
        int64_t value = proc_parse_kv_kb(line, key, sizeof(key));
        
        if (value >= 0) {
            if (strcmp(key, "MemTotal") == 0) data->mem_total_kb = value;
            else if (strcmp(key, "MemAvailable") == 0) data->mem_available_kb = value;
            else if (strcmp(key, "MemFree") == 0) data->mem_free_kb = value;
            else if (strcmp(key, "Buffers") == 0) data->buffers_kb = value;
            else if (strcmp(key, "Cached") == 0) data->cached_kb = value;
            else if (strcmp(key, "Slab") == 0) data->slab_kb = value;
            else if (strcmp(key, "SReclaimable") == 0) data->sreclaimable_kb = value;
            else if (strcmp(key, "SUnreclaim") == 0) data->sunreclaim_kb = value;
            else if (strcmp(key, "Active") == 0) data->active_kb = value;
            else if (strcmp(key, "Inactive") == 0) data->inactive_kb = value;
            else if (strcmp(key, "AnonPages") == 0) data->anon_pages_kb = value;
            else if (strcmp(key, "VmallocUsed") == 0) data->vmalloc_used_kb = value;
            else if (strcmp(key, "PageTables") == 0) data->page_tables_kb = value;
            else if (strcmp(key, "KernelStack") == 0) data->kernel_stack_kb = value;
            else if (strcmp(key, "Dirty") == 0) data->dirty_kb = value;
            else if (strcmp(key, "Mapped") == 0) data->mapped_kb = value;
        }
        
        /* Next line */
        line = strchr(line, '\n');
        if (!line) break;
        line++;
    }
    
    /* Calculate usage percentage */
    if (data->mem_total_kb > 0) {
        int64_t used = data->mem_total_kb - data->mem_available_kb;
        data->usage_percent = (double)used * 100.0 / (double)data->mem_total_kb;
    }
    
    return 0;
}

static int meminfo_collect(qmem_service_t *svc) {
    meminfo_priv_t *priv = (meminfo_priv_t *)svc->priv;
    
    /* Save previous */
    priv->previous = priv->current;
    priv->has_previous = true;
    
    /* Collect new */
    return parse_meminfo(&priv->current);
}

static void write_delta(json_builder_t *j, const char *name, 
                        int64_t current, int64_t previous, bool has_prev) {
    json_key(j, name);
    json_object_start(j);
    json_kv_int(j, "value", current);
    if (has_prev) {
        json_kv_int(j, "delta", current - previous);
    }
    json_object_end(j);
}

static int meminfo_snapshot(qmem_service_t *svc, json_builder_t *j) {
    meminfo_priv_t *priv = (meminfo_priv_t *)svc->priv;
    meminfo_data_t *cur = &priv->current;
    meminfo_data_t *prev = &priv->previous;
    bool has_prev = priv->has_previous;
    
    json_object_start(j);
    
    json_kv_double(j, "usage_percent", cur->usage_percent);
    
    json_key(j, "memory");
    json_object_start(j);
    write_delta(j, "total_kb", cur->mem_total_kb, prev->mem_total_kb, has_prev);
    write_delta(j, "available_kb", cur->mem_available_kb, prev->mem_available_kb, has_prev);
    write_delta(j, "free_kb", cur->mem_free_kb, prev->mem_free_kb, has_prev);
    write_delta(j, "buffers_kb", cur->buffers_kb, prev->buffers_kb, has_prev);
    write_delta(j, "cached_kb", cur->cached_kb, prev->cached_kb, has_prev);
    json_object_end(j);
    
    json_key(j, "kernel");
    json_object_start(j);
    write_delta(j, "slab_kb", cur->slab_kb, prev->slab_kb, has_prev);
    write_delta(j, "sreclaimable_kb", cur->sreclaimable_kb, prev->sreclaimable_kb, has_prev);
    write_delta(j, "sunreclaim_kb", cur->sunreclaim_kb, prev->sunreclaim_kb, has_prev);
    write_delta(j, "vmalloc_used_kb", cur->vmalloc_used_kb, prev->vmalloc_used_kb, has_prev);
    write_delta(j, "page_tables_kb", cur->page_tables_kb, prev->page_tables_kb, has_prev);
    write_delta(j, "kernel_stack_kb", cur->kernel_stack_kb, prev->kernel_stack_kb, has_prev);
    json_object_end(j);
    
    json_key(j, "activity");
    json_object_start(j);
    write_delta(j, "active_kb", cur->active_kb, prev->active_kb, has_prev);
    write_delta(j, "inactive_kb", cur->inactive_kb, prev->inactive_kb, has_prev);
    write_delta(j, "anon_pages_kb", cur->anon_pages_kb, prev->anon_pages_kb, has_prev);
    write_delta(j, "dirty_kb", cur->dirty_kb, prev->dirty_kb, has_prev);
    write_delta(j, "mapped_kb", cur->mapped_kb, prev->mapped_kb, has_prev);
    json_object_end(j);
    
    json_object_end(j);
    
    return 0;
}

static void meminfo_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("meminfo service destroyed");
}

static const qmem_service_ops_t meminfo_ops = {
    .init = meminfo_init,
    .collect = meminfo_collect,
    .snapshot = meminfo_snapshot,
    .destroy = meminfo_destroy,
};

qmem_service_t meminfo_service = {
    .name = "meminfo",
    .description = "System memory info from /proc/meminfo",
    .ops = &meminfo_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

const meminfo_data_t *meminfo_get_current(void) {
    return &g_meminfo.current;
}

const meminfo_data_t *meminfo_get_previous(void) {
    return g_meminfo.has_previous ? &g_meminfo.previous : NULL;
}
