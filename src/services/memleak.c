/*
 * memleak.c - Unified memory leak detection (Kernel + User)
 * Aggregates slabinfo, procmem, and heapmon.
 */
#include "service.h"
#include "common/log.h"
#include "common/json.h"
#include "services/procmem.h"
#include "services/slabinfo.h"
#include "services/heapmon.h"
#include <qmem/plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int dummy; // Placeholder
} memleak_priv_t;

static memleak_priv_t g_memleak;

static int memleak_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)svc;
    memset(&g_memleak, 0, sizeof(g_memleak));
    svc->priv = &g_memleak;
    
    /* Initialize subsystems */
    /* We pass the main config to them, though they might not use it effectively if not main service */
    if (procmem_service.ops->init) procmem_service.ops->init(&procmem_service, cfg);
    if (slabinfo_service.ops->init) slabinfo_service.ops->init(&slabinfo_service, cfg);
    if (heapmon_service.ops->init) heapmon_service.ops->init(&heapmon_service, cfg);
    
    log_debug("memleak service initialized (unifying procmem, slabinfo, heapmon)");
    return 0;
}

static int memleak_collect(qmem_service_t *svc) {
    (void)svc;
    
    /* Collect data from all sources */
    if (procmem_service.ops->collect) procmem_service.ops->collect(&procmem_service);
    if (slabinfo_service.ops->collect) slabinfo_service.ops->collect(&slabinfo_service);
    if (heapmon_service.ops->collect) heapmon_service.ops->collect(&heapmon_service);
    
    return 0;
}

static int memleak_snapshot(qmem_service_t *svc, json_builder_t *j) {
    (void)svc;
    
    json_object_start(j);
    
    /* Kernel Leaks (Slab) */
    json_key(j, "kernel_leaks");
    json_array_start(j);
    {
        slab_entry_t entries[10];
        int n = slabinfo_get_top_growers(entries, 10);
        for (int i = 0; i < n; i++) {
            json_object_start(j);
            json_kv_string(j, "cache", entries[i].name);
            json_kv_int(j, "delta_bytes", entries[i].delta_bytes);
            json_kv_int(j, "total_bytes", entries[i].size_bytes);
            json_object_end(j);
        }
    }
    json_array_end(j);
    
    /* User Leaks (Process RSS + Heap) */
    /* We use heapmon results as they contain both RSS and Heap info for top growers */
    json_key(j, "user_leaks");
    json_array_start(j);
    {
        heapmon_entry_t entries[10];
        int n = heapmon_get_entries(entries, 10);
        for (int i = 0; i < n; i++) {
            json_object_start(j);
            json_kv_int(j, "pid", entries[i].pid);
            json_kv_string(j, "cmd", entries[i].cmd);
            json_kv_int(j, "rss_kb", entries[i].rss_kb);
            json_kv_int(j, "rss_delta_kb", entries[i].rss_delta_kb);
            json_kv_int(j, "heap_rss_kb", entries[i].heap_rss_kb);
            json_kv_int(j, "heap_delta_kb", entries[i].heap_rss_delta_kb);
            json_kv_int(j, "heap_pd_kb", entries[i].heap_private_dirty_kb);
            json_kv_int(j, "heap_pd_delta_kb", entries[i].heap_pd_delta_kb);
            json_kv_int(j, "heap_size_kb", entries[i].heap_size_kb);
            json_object_end(j);
        }
    }

    json_array_end(j);

    /* Top Process Usage (Absolute) */
    json_key(j, "process_usage");
    json_array_start(j);
    {
        heapmon_entry_t entries[10];
        int n = heapmon_get_top_consumers(entries, 10);
        for (int i = 0; i < n; i++) {
            json_object_start(j);
            json_kv_int(j, "pid", entries[i].pid);
            json_kv_string(j, "cmd", entries[i].cmd);
            json_kv_int(j, "rss_kb", entries[i].rss_kb);
            json_kv_int(j, "rss_delta_kb", entries[i].rss_delta_kb);
            json_kv_int(j, "heap_rss_kb", entries[i].heap_rss_kb);
            json_kv_int(j, "heap_delta_kb", entries[i].heap_rss_delta_kb);
            json_kv_int(j, "heap_pd_kb", entries[i].heap_private_dirty_kb);
            json_kv_int(j, "heap_size_kb", entries[i].heap_size_kb);
            json_object_end(j);
        }
    }
    json_array_end(j);

    /* Top Kernel Usage (Absolute Slab) */
    json_key(j, "kernel_usage");
    json_array_start(j);
    {
        slab_entry_t entries[10];
        int n = slabinfo_get_top_consumers(entries, 10);
        for (int i = 0; i < n; i++) {
            json_object_start(j);
            json_kv_string(j, "cache", entries[i].name);
            json_kv_int(j, "total_bytes", entries[i].size_bytes);
            json_kv_int(j, "delta_bytes", entries[i].delta_bytes);
            json_kv_int(j, "active_objs", entries[i].num_objs);
            json_object_end(j);
        }
    }
    json_array_end(j);
    
    json_object_end(j);
    return 0;
}

static void memleak_destroy(qmem_service_t *svc) {
    (void)svc;
    if (heapmon_service.ops->destroy) heapmon_service.ops->destroy(&heapmon_service);
    if (slabinfo_service.ops->destroy) slabinfo_service.ops->destroy(&slabinfo_service);
    if (procmem_service.ops->destroy) procmem_service.ops->destroy(&procmem_service);
    log_debug("memleak service destroyed");
}

static const qmem_service_ops_t memleak_ops = {
    .init = memleak_init,
    .collect = memleak_collect,
    .snapshot = memleak_snapshot,
    .destroy = memleak_destroy,
};

qmem_service_t memleak_service = {
    .name = "memleak",
    .description = "Unified memory leak detection (Kernel Slabs + User Processes)",
    .ops = &memleak_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

QMEM_PLUGIN_DEFINE("memleak", "1.0", "Unified Memory Leak Detector", memleak_service);
