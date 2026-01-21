/*
 * service.h - Service/plugin interface
 * 
 * Each monitoring service implements this interface to be
 * registered with the service manager.
 */
#ifndef QMEM_SERVICE_H
#define QMEM_SERVICE_H

#include <stdbool.h>
#include "common/json.h"

/* Forward declarations */
typedef struct qmem_config qmem_config_t;
typedef struct qmem_service qmem_service_t;

/*
 * Service lifecycle callbacks
 */
typedef struct {
    /* Initialize service with configuration */
    int (*init)(qmem_service_t *svc, const qmem_config_t *cfg);
    
    /* Collect current data (called each interval) */
    int (*collect)(qmem_service_t *svc);
    
    /* Write current snapshot to JSON builder */
    int (*snapshot)(qmem_service_t *svc, json_builder_t *json);
    
    /* Cleanup and free resources */
    void (*destroy)(qmem_service_t *svc);
} qmem_service_ops_t;

/*
 * Service structure
 */
struct qmem_service {
    const char *name;           /* Service identifier */
    const char *description;    /* Human-readable description */
    const qmem_service_ops_t *ops;
    void *priv;                 /* Service-private data */
    bool enabled;
    int collect_count;          /* Number of collections performed */
};

/* Macro to define a service */
#define QMEM_SERVICE_DEFINE(svc_name, desc, ops_ptr) \
    qmem_service_t svc_name = { \
        .name = #svc_name, \
        .description = desc, \
        .ops = ops_ptr, \
        .priv = NULL, \
        .enabled = true, \
        .collect_count = 0, \
    }

/* Service initialization helpers */
int qmem_service_init(qmem_service_t *svc, const qmem_config_t *cfg);
int qmem_service_collect(qmem_service_t *svc);
int qmem_service_snapshot(qmem_service_t *svc, json_builder_t *json);
void qmem_service_destroy(qmem_service_t *svc);

/* Built-in services */
extern qmem_service_t meminfo_service;
extern qmem_service_t slabinfo_service;
extern qmem_service_t procmem_service;
extern qmem_service_t heapmon_service;
extern qmem_service_t vmstat_service;

#endif /* QMEM_SERVICE_H */
