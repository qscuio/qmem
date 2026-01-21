/*
 * plugin_loader.h - Dynamic plugin loader
 */
#ifndef QMEM_PLUGIN_LOADER_H
#define QMEM_PLUGIN_LOADER_H

#include "config.h"
#include <stdbool.h>

/* Initialize plugin loader */
int plugin_loader_init(const qmem_config_t *cfg);

/* Load all plugins from directory */
int plugin_loader_load_all(void);

/* Load a specific plugin by path */
int plugin_loader_load(const char *path);

/* Unload a plugin by name */
int plugin_loader_unload(const char *name);

/* Reload a plugin (unload + load) */
int plugin_loader_reload(const char *path);

/* Start watching plugin directory for changes */
int plugin_loader_start_watcher(void);

/* Stop watcher and unload all plugins */
void plugin_loader_shutdown(void);

/* Get number of loaded plugins */
int plugin_loader_count(void);

/* Check for and process file changes (call from main loop) */
void plugin_loader_check_updates(void);

#endif /* QMEM_PLUGIN_LOADER_H */
