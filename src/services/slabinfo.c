/*
 * slabinfo.c - /proc/slabinfo monitor implementation
 */
#include "slabinfo.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include "common/format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qmem/plugin.h>

#define MAX_SLABS 512
#define TOP_N 20
#define MIN_DELTA_BYTES (512 * 1024)  /* 512 KB */

typedef struct {
    char name[64];
    int64_t size_bytes;
    int32_t num_objs;
    int32_t obj_size;
} slab_cache_info_t;

typedef struct {
    slab_cache_info_t current[MAX_SLABS];
    slab_cache_info_t previous[MAX_SLABS];
    int current_count;
    int previous_count;
    bool has_previous;
    
    /* Sorted results */
    slab_entry_t growers[TOP_N];
    slab_entry_t shrinkers[TOP_N];
    int grower_count;
    int shrinker_count;
} slabinfo_priv_t;

static slabinfo_priv_t g_slabinfo;

static int slabinfo_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_slabinfo, 0, sizeof(g_slabinfo));
    svc->priv = &g_slabinfo;
    
    log_debug("slabinfo service initialized");
    return 0;
}

static int parse_slabinfo(slab_cache_info_t *slabs, int *count) {
    char buf[65536];
    
    if (proc_read_file("/proc/slabinfo", buf, sizeof(buf)) < 0) {
        log_error("Failed to read /proc/slabinfo");
        return -1;
    }
    
    *count = 0;
    char *line = buf;
    
    while (*line && *count < MAX_SLABS) {
        /* Skip header lines */
        if (strncmp(line, "slabinfo", 8) == 0 || line[0] == '#') {
            line = strchr(line, '\n');
            if (!line) break;
            line++;
            continue;
        }
        
        /* Parse: name active_objs num_objs objsize ... */
        slab_cache_info_t *entry = &slabs[*count];
        int active_objs, num_objs, objsize;
        
        if (sscanf(line, "%63s %d %d %d", 
                   entry->name, &active_objs, &num_objs, &objsize) == 4) {
            entry->num_objs = num_objs;
            entry->obj_size = objsize;
            entry->size_bytes = (int64_t)num_objs * objsize;
            (*count)++;
        }
        
        /* Next line */
        line = strchr(line, '\n');
        if (!line) break;
        line++;
    }
    
    return 0;
}

static slab_cache_info_t *find_slab(slab_cache_info_t *slabs, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(slabs[i].name, name) == 0) {
            return &slabs[i];
        }
    }
    return NULL;
}

static int compare_growers(const void *a, const void *b) {
    const slab_entry_t *ea = (const slab_entry_t *)a;
    const slab_entry_t *eb = (const slab_entry_t *)b;
    /* Sort by delta descending */
    if (eb->delta_bytes > ea->delta_bytes) return 1;
    if (eb->delta_bytes < ea->delta_bytes) return -1;
    return 0;
}

static int compare_shrinkers(const void *a, const void *b) {
    const slab_entry_t *ea = (const slab_entry_t *)a;
    const slab_entry_t *eb = (const slab_entry_t *)b;
    /* Sort by delta ascending (most negative first) */
    if (ea->delta_bytes < eb->delta_bytes) return -1;
    if (ea->delta_bytes > eb->delta_bytes) return 1;
    return 0;
}

static int slabinfo_collect(qmem_service_t *svc) {
    slabinfo_priv_t *priv = (slabinfo_priv_t *)svc->priv;
    
    /* Save previous */
    memcpy(priv->previous, priv->current, sizeof(priv->previous));
    priv->previous_count = priv->current_count;
    
    /* Collect new */
    int ret = parse_slabinfo(priv->current, &priv->current_count);
    if (ret < 0) return ret;
    
    /* Calculate deltas and sort into growers/shrinkers */
    priv->grower_count = 0;
    priv->shrinker_count = 0;
    
    if (priv->has_previous) {
        slab_entry_t all_changes[MAX_SLABS];
        int change_count = 0;
        
        for (int i = 0; i < priv->current_count; i++) {
            slab_cache_info_t *cur = &priv->current[i];
            slab_cache_info_t *prev = find_slab(priv->previous, priv->previous_count, cur->name);
            
            if (prev) {
                int64_t delta = cur->size_bytes - prev->size_bytes;
                int64_t abs_delta = delta < 0 ? -delta : delta;
                
                if (abs_delta >= MIN_DELTA_BYTES) {
                    slab_entry_t *e = &all_changes[change_count++];
                    strncpy(e->name, cur->name, sizeof(e->name) - 1);
                    e->size_bytes = cur->size_bytes;
                    e->delta_bytes = delta;
                    e->num_objs = cur->num_objs;
                    e->obj_size = cur->obj_size;
                }
            }
        }
        
        /* Sort and take top growers */
        qsort(all_changes, change_count, sizeof(slab_entry_t), compare_growers);
        for (int i = 0; i < change_count && priv->grower_count < TOP_N; i++) {
            if (all_changes[i].delta_bytes > 0) {
                priv->growers[priv->grower_count++] = all_changes[i];
            }
        }
        
        /* Sort and take top shrinkers */
        qsort(all_changes, change_count, sizeof(slab_entry_t), compare_shrinkers);
        for (int i = 0; i < change_count && priv->shrinker_count < TOP_N; i++) {
            if (all_changes[i].delta_bytes < 0) {
                priv->shrinkers[priv->shrinker_count++] = all_changes[i];
            }
        }
    }
    
    priv->has_previous = true;
    return 0;
}

static int slabinfo_snapshot(qmem_service_t *svc, json_builder_t *j) {
    slabinfo_priv_t *priv = (slabinfo_priv_t *)svc->priv;
    
    json_object_start(j);
    
    json_kv_int(j, "total_caches", priv->current_count);
    
    json_key(j, "top_growers");
    json_array_start(j);
    for (int i = 0; i < priv->grower_count; i++) {
        slab_entry_t *e = &priv->growers[i];
        json_object_start(j);
        json_kv_string(j, "name", e->name);
        json_kv_int(j, "size_bytes", e->size_bytes);
        json_kv_int(j, "delta_bytes", e->delta_bytes);
        json_kv_int(j, "num_objs", e->num_objs);
        json_kv_int(j, "obj_size", e->obj_size);
        json_object_end(j);
    }
    json_array_end(j);
    
    json_key(j, "top_shrinkers");
    json_array_start(j);
    for (int i = 0; i < priv->shrinker_count; i++) {
        slab_entry_t *e = &priv->shrinkers[i];
        json_object_start(j);
        json_kv_string(j, "name", e->name);
        json_kv_int(j, "size_bytes", e->size_bytes);
        json_kv_int(j, "delta_bytes", e->delta_bytes);
        json_kv_int(j, "num_objs", e->num_objs);
        json_kv_int(j, "obj_size", e->obj_size);
        json_object_end(j);
    }
    json_array_end(j);
    
    json_object_end(j);
    
    return 0;
}

static void slabinfo_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("slabinfo service destroyed");
}

static const qmem_service_ops_t slabinfo_ops = {
    .init = slabinfo_init,
    .collect = slabinfo_collect,
    .snapshot = slabinfo_snapshot,
    .destroy = slabinfo_destroy,
};

qmem_service_t slabinfo_service = {
    .name = "slabinfo",
    .description = "Slab cache info from /proc/slabinfo",
    .ops = &slabinfo_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

QMEM_PLUGIN_DEFINE("slabinfo", "1.0", "Slab cache monitor", slabinfo_service);

int slabinfo_get_top_growers(slab_entry_t *entries, int max_entries) {
    int n = g_slabinfo.grower_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_slabinfo.growers, n * sizeof(slab_entry_t));
    return n;
}

int slabinfo_get_top_shrinkers(slab_entry_t *entries, int max_entries) {
    int n = g_slabinfo.shrinker_count;
    if (n > max_entries) n = max_entries;
    memcpy(entries, g_slabinfo.shrinkers, n * sizeof(slab_entry_t));
    return n;
}
