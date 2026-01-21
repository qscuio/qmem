/*
 * vmstat.c - /proc/vmstat monitor implementation
 */
#include "vmstat.h"
#include "common/log.h"
#include "common/proc_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qmem/plugin.h>

typedef struct {
    vmstat_data_t current;
} vmstat_priv_t;

static vmstat_priv_t g_vmstat;

static int vmstat_init(qmem_service_t *svc, const qmem_config_t *cfg) {
    (void)cfg;
    
    memset(&g_vmstat, 0, sizeof(g_vmstat));
    svc->priv = &g_vmstat;
    
    log_debug("vmstat service initialized");
    return 0;
}

static int parse_vmstat(vmstat_data_t *data) {
    char buf[32768];
    
    if (proc_read_file("/proc/vmstat", buf, sizeof(buf)) < 0) {
        log_error("Failed to read /proc/vmstat");
        return -1;
    }
    
    memset(data, 0, sizeof(*data));
    
    char *line = buf;
    while (*line) {
        char key[64];
        int64_t value;
        
        if (sscanf(line, "%63s %ld", key, &value) == 2) {
            if (strcmp(key, "nr_slab_unreclaimable") == 0) {
                data->nr_slab_unreclaimable = value;
            } else if (strcmp(key, "nr_slab_reclaimable") == 0) {
                data->nr_slab_reclaimable = value;
            } else if (strcmp(key, "nr_vmscan_write") == 0) {
                /* vmalloc is sometimes named differently */
            } else if (strcmp(key, "nr_kernel_stack") == 0) {
                data->nr_kernel_stack = value;
            } else if (strcmp(key, "nr_page_table_pages") == 0) {
                data->nr_page_table_pages = value;
            } else if (strcmp(key, "nr_dirty") == 0) {
                data->nr_dirty = value;
            } else if (strcmp(key, "nr_writeback") == 0) {
                data->nr_writeback = value;
            }
        }
        
        /* Next line */
        line = strchr(line, '\n');
        if (!line) break;
        line++;
    }
    
    return 0;
}

static int vmstat_collect(qmem_service_t *svc) {
    vmstat_priv_t *priv = (vmstat_priv_t *)svc->priv;
    return parse_vmstat(&priv->current);
}

static int vmstat_snapshot(qmem_service_t *svc, json_builder_t *j) {
    vmstat_priv_t *priv = (vmstat_priv_t *)svc->priv;
    vmstat_data_t *data = &priv->current;
    
    json_object_start(j);
    json_kv_int(j, "nr_slab_unreclaimable", data->nr_slab_unreclaimable);
    json_kv_int(j, "nr_slab_reclaimable", data->nr_slab_reclaimable);
    json_kv_int(j, "nr_vmalloc", data->nr_vmalloc);
    json_kv_int(j, "nr_kernel_stack", data->nr_kernel_stack);
    json_kv_int(j, "nr_page_table_pages", data->nr_page_table_pages);
    json_kv_int(j, "nr_dirty", data->nr_dirty);
    json_kv_int(j, "nr_writeback", data->nr_writeback);
    json_object_end(j);
    
    return 0;
}

static void vmstat_destroy(qmem_service_t *svc) {
    (void)svc;
    log_debug("vmstat service destroyed");
}

static const qmem_service_ops_t vmstat_ops = {
    .init = vmstat_init,
    .collect = vmstat_collect,
    .snapshot = vmstat_snapshot,
    .destroy = vmstat_destroy,
};

qmem_service_t vmstat_service = {
    .name = "vmstat",
    .description = "Kernel VM stats from /proc/vmstat",
    .ops = &vmstat_ops,
    .priv = NULL,
    .enabled = true,
    .collect_count = 0,
};

QMEM_PLUGIN_DEFINE("vmstat", "1.0", "Virtual memory statistics", vmstat_service);

const vmstat_data_t *vmstat_get_current(void) {
    return &g_vmstat.current;
}
