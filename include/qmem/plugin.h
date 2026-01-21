/*
 * plugin.h - Plugin interface definition
 * 
 * All plugins must export a qmem_plugin_info symbol
 */
#ifndef QMEM_PLUGIN_H
#define QMEM_PLUGIN_H

#include "services/service.h"

#define QMEM_PLUGIN_API_VERSION 1

/* Plugin info structure - each plugin exports this */
typedef struct {
    int api_version;               /* Must match QMEM_PLUGIN_API_VERSION */
    const char *name;              /* Plugin name */
    const char *version;           /* Plugin version string */
    const char *description;       /* Plugin description */
    qmem_service_t *service;       /* The service this plugin provides */
} qmem_plugin_info_t;

/* Symbol name that plugins must export */
#define QMEM_PLUGIN_SYMBOL "qmem_plugin_info"

/* Macro to define plugin info */
#define QMEM_PLUGIN_DEFINE(NAME, VERSION, DESC, SERVICE) \
    qmem_plugin_info_t qmem_plugin_info = { \
        .api_version = QMEM_PLUGIN_API_VERSION, \
        .name = NAME, \
        .version = VERSION, \
        .description = DESC, \
        .service = &SERVICE, \
    }

#endif /* QMEM_PLUGIN_H */
