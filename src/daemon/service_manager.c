/*
 * service_manager.c - Service registry and lifecycle management
 */
#include "service_manager.h"
#include "common/log.h"
#include <string.h>
#include <time.h>

static qmem_service_t *g_services[MAX_SERVICES];
static int g_service_count = 0;
static const qmem_config_t *g_config = NULL;

int svc_manager_init(const qmem_config_t *cfg) {
    g_service_count = 0;
    g_config = cfg;
    
    memset(g_services, 0, sizeof(g_services));
    
    log_info("Service manager initialized");
    return 0;
}

int svc_manager_register(qmem_service_t *svc) {
    if (g_service_count >= MAX_SERVICES) {
        log_error("Cannot register service %s: max services reached", svc->name);
        return -1;
    }
    
    /* Initialize the service */
    if (svc->ops && svc->ops->init) {
        int ret = svc->ops->init(svc, g_config);
        if (ret < 0) {
            log_error("Failed to initialize service %s", svc->name);
            return ret;
        }
    }
    
    g_services[g_service_count++] = svc;
    log_info("Registered service: %s (%s)", svc->name, svc->description);
    
    return 0;
}

int svc_manager_unregister(qmem_service_t *svc) {
    int idx = -1;
    for (int i = 0; i < g_service_count; i++) {
        if (g_services[i] == svc) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        log_warn("Service not found for unregister: %s", svc->name);
        return -1;
    }
    
    /* Destroy the service */
    if (svc->ops && svc->ops->destroy) {
        svc->ops->destroy(svc);
    }
    
    /* Remove from array by shifting */
    for (int i = idx; i < g_service_count - 1; i++) {
        g_services[i] = g_services[i + 1];
    }
    g_service_count--;
    
    log_info("Unregistered service: %s", svc->name);
    return 0;
}

int svc_manager_count(void) {
    return g_service_count;
}

qmem_service_t *svc_manager_get(const char *name) {
    for (int i = 0; i < g_service_count; i++) {
        if (strcmp(g_services[i]->name, name) == 0) {
            return g_services[i];
        }
    }
    return NULL;
}

qmem_service_t *svc_manager_get_index(int index) {
    if (index < 0 || index >= g_service_count) {
        return NULL;
    }
    return g_services[index];
}

int svc_manager_collect_all(void) {
    int errors = 0;
    
    for (int i = 0; i < g_service_count; i++) {
        qmem_service_t *svc = g_services[i];
        
        if (!svc->enabled) continue;
        
        if (svc->ops && svc->ops->collect) {
            int ret = svc->ops->collect(svc);
            if (ret < 0) {
                log_warn("Service %s collect failed: %d", svc->name, ret);
                errors++;
            } else {
                svc->collect_count++;
            }
        }
    }
    
    return errors;
}

int svc_manager_snapshot_all(json_builder_t *json) {
    json_object_start(json);
    
    /* Add timestamp */
    time_t now = time(NULL);
    json_kv_int(json, "timestamp", (int64_t)now);
    
    /* Add services */
    json_key(json, "services");
    json_object_start(json);
    
    for (int i = 0; i < g_service_count; i++) {
        qmem_service_t *svc = g_services[i];
        
        if (!svc->enabled) continue;
        
        json_key(json, svc->name);
        
        if (svc->ops && svc->ops->snapshot) {
            svc->ops->snapshot(svc, json);
        } else {
            json_null(json);
        }
    }
    
    json_object_end(json);  /* services */
    json_object_end(json);  /* root */
    
    return 0;
}

void svc_manager_shutdown(void) {
    for (int i = 0; i < g_service_count; i++) {
        qmem_service_t *svc = g_services[i];
        
        if (svc->ops && svc->ops->destroy) {
            svc->ops->destroy(svc);
        }
    }
    
    g_service_count = 0;
    log_info("Service manager shutdown");
}
