/*
 * service_manager.h - Service registry and lifecycle management
 */
#ifndef QMEM_SERVICE_MANAGER_H
#define QMEM_SERVICE_MANAGER_H

#include "services/service.h"
#include "config.h"
#include "common/json.h"

#define MAX_SERVICES 16

/* Initialize service manager */
int svc_manager_init(const qmem_config_t *cfg);

/* Register a service */
int svc_manager_register(qmem_service_t *svc);

/* Unregister a service (for plugin hot-reload) */
int svc_manager_unregister(qmem_service_t *svc);

/* Get number of registered services */
int svc_manager_count(void);

/* Get service by name */
qmem_service_t *svc_manager_get(const char *name);

/* Get service by index */
qmem_service_t *svc_manager_get_index(int index);

/* Collect from all services */
int svc_manager_collect_all(void);

/* Generate full snapshot JSON */
int svc_manager_snapshot_all(json_builder_t *json);

/* Shutdown all services */
void svc_manager_shutdown(void);

#endif /* QMEM_SERVICE_MANAGER_H */
