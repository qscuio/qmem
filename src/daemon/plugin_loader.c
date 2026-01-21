/*
 * plugin_loader.c - Dynamic plugin loader implementation
 * 
 * Uses dlopen/dlsym for loading and inotify for hot-reload
 */
#define _GNU_SOURCE
#include "plugin_loader.h"
#include "service_manager.h"
#include "common/log.h"
#include <qmem/plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define MAX_PLUGINS 64
#define PLUGIN_DIR_DEFAULT "/usr/lib/qmem/plugins"

/* Loaded plugin info */
typedef struct {
    char path[512];
    char name[64];
    void *handle;                  /* dlopen handle */
    qmem_plugin_info_t *info;
    qmem_service_t *service;
    time_t mtime;                  /* Last modification time */
    bool loaded;
} loaded_plugin_t;

typedef struct {
    char plugin_dir[512];
    loaded_plugin_t plugins[MAX_PLUGINS];
    int plugin_count;
    int inotify_fd;
    int watch_fd;
    bool watcher_running;
} plugin_loader_t;

static plugin_loader_t g_loader;

int plugin_loader_init(const qmem_config_t *cfg) {
    memset(&g_loader, 0, sizeof(g_loader));
    g_loader.inotify_fd = -1;
    g_loader.watch_fd = -1;
    
    /* Set plugin directory */
    const char *dir = cfg->plugin_dir[0] ? cfg->plugin_dir : PLUGIN_DIR_DEFAULT;
    strncpy(g_loader.plugin_dir, dir, sizeof(g_loader.plugin_dir) - 1);
    
    log_info("Plugin loader initialized (dir=%s)", g_loader.plugin_dir);
    return 0;
}

static time_t get_file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    return st.st_mtime;
}

static loaded_plugin_t *find_plugin_by_path(const char *path) {
    for (int i = 0; i < g_loader.plugin_count; i++) {
        if (strcmp(g_loader.plugins[i].path, path) == 0) {
            return &g_loader.plugins[i];
        }
    }
    return NULL;
}

static loaded_plugin_t *find_plugin_by_name(const char *name) {
    for (int i = 0; i < g_loader.plugin_count; i++) {
        if (strcmp(g_loader.plugins[i].name, name) == 0) {
            return &g_loader.plugins[i];
        }
    }
    return NULL;
}

int plugin_loader_load(const char *path) {
    /* Check if already loaded */
    loaded_plugin_t *existing = find_plugin_by_path(path);
    if (existing && existing->loaded) {
        log_debug("Plugin already loaded: %s", path);
        return 0;
    }
    
    /* Check capacity */
    if (g_loader.plugin_count >= MAX_PLUGINS) {
        log_error("Max plugins reached (%d)", MAX_PLUGINS);
        return -1;
    }
    
    /* Open shared library */
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        log_error("Failed to load plugin %s: %s", path, dlerror());
        return -1;
    }
    
    /* Find plugin info symbol */
    qmem_plugin_info_t *info = (qmem_plugin_info_t *)dlsym(handle, QMEM_PLUGIN_SYMBOL);
    if (!info) {
        log_error("Plugin %s missing symbol '%s'", path, QMEM_PLUGIN_SYMBOL);
        dlclose(handle);
        return -1;
    }
    
    /* Verify API version */
    if (info->api_version != QMEM_PLUGIN_API_VERSION) {
        log_error("Plugin %s API version mismatch: got %d, expected %d",
                  path, info->api_version, QMEM_PLUGIN_API_VERSION);
        dlclose(handle);
        return -1;
    }
    
    /* Check for duplicate name */
    loaded_plugin_t *dup = find_plugin_by_name(info->name);
    if (dup && dup->loaded) {
        log_warn("Plugin with name '%s' already loaded, skipping %s", info->name, path);
        dlclose(handle);
        return -1;
    }
    
    /* Register the service */
    if (svc_manager_register(info->service) < 0) {
        log_error("Failed to register service from plugin %s", path);
        dlclose(handle);
        return -1;
    }
    
    /* Store plugin info */
    loaded_plugin_t *plugin;
    if (existing) {
        plugin = existing;
    } else {
        plugin = &g_loader.plugins[g_loader.plugin_count++];
    }
    
    strncpy(plugin->path, path, sizeof(plugin->path) - 1);
    strncpy(plugin->name, info->name, sizeof(plugin->name) - 1);
    plugin->handle = handle;
    plugin->info = info;
    plugin->service = info->service;
    plugin->mtime = get_file_mtime(path);
    plugin->loaded = true;
    
    log_info("Loaded plugin: %s v%s (%s)", info->name, info->version, info->description);
    return 0;
}

int plugin_loader_unload(const char *name) {
    loaded_plugin_t *plugin = find_plugin_by_name(name);
    if (!plugin || !plugin->loaded) {
        log_warn("Plugin not found: %s", name);
        return -1;
    }
    
    /* Unregister service */
    svc_manager_unregister(plugin->service);
    
    /* Close library */
    if (plugin->handle) {
        dlclose(plugin->handle);
        plugin->handle = NULL;
    }
    
    plugin->loaded = false;
    log_info("Unloaded plugin: %s", name);
    return 0;
}

int plugin_loader_reload(const char *path) {
    loaded_plugin_t *plugin = find_plugin_by_path(path);
    if (plugin && plugin->loaded) {
        log_info("Reloading plugin: %s", plugin->name);
        plugin_loader_unload(plugin->name);
    }
    
    return plugin_loader_load(path);
}

int plugin_loader_load_all(void) {
    /* Create plugin directory if it doesn't exist */
    struct stat st;
    if (stat(g_loader.plugin_dir, &st) < 0) {
        if (errno == ENOENT) {
            if (mkdir(g_loader.plugin_dir, 0755) == 0) {
                log_info("Created plugin directory: %s", g_loader.plugin_dir);
            } else {
                log_debug("Cannot create plugin directory %s: %s", 
                          g_loader.plugin_dir, strerror(errno));
            }
        }
    }
    
    DIR *dir = opendir(g_loader.plugin_dir);
    if (!dir) {
        if (errno == ENOENT) {
            log_debug("Plugin directory does not exist: %s", g_loader.plugin_dir);
            return 0;
        }
        log_error("Failed to open plugin directory %s: %s", 
                  g_loader.plugin_dir, strerror(errno));
        return -1;
    }
    
    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        /* Skip non-.so files */
        size_t len = strlen(ent->d_name);
        if (len < 4 || strcmp(ent->d_name + len - 3, ".so") != 0) {
            continue;
        }
        
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", g_loader.plugin_dir, ent->d_name);
        
        if (plugin_loader_load(path) == 0) {
            loaded++;
        }
    }
    
    closedir(dir);
    if (loaded > 0) {
        log_info("Loaded %d plugins from %s", loaded, g_loader.plugin_dir);
    }
    return loaded;
}

int plugin_loader_start_watcher(void) {
    g_loader.inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (g_loader.inotify_fd < 0) {
        log_warn("Failed to initialize inotify: %s", strerror(errno));
        return -1;
    }
    
    g_loader.watch_fd = inotify_add_watch(g_loader.inotify_fd, g_loader.plugin_dir,
                                           IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);
    if (g_loader.watch_fd < 0) {
        log_warn("Failed to watch plugin directory: %s", strerror(errno));
        close(g_loader.inotify_fd);
        g_loader.inotify_fd = -1;
        return -1;
    }
    
    g_loader.watcher_running = true;
    log_info("Watching plugin directory for changes");
    return 0;
}

void plugin_loader_check_updates(void) {
    if (g_loader.inotify_fd < 0) return;
    
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    
    ssize_t len = read(g_loader.inotify_fd, buf, sizeof(buf));
    if (len <= 0) return;
    
    const struct inotify_event *event;
    for (char *ptr = buf; ptr < buf + len; 
         ptr += sizeof(struct inotify_event) + event->len) {
        event = (const struct inotify_event *)ptr;
        
        if (event->len == 0) continue;
        
        /* Only handle .so files */
        size_t name_len = strlen(event->name);
        if (name_len < 4 || strcmp(event->name + name_len - 3, ".so") != 0) {
            continue;
        }
        
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", g_loader.plugin_dir, event->name);
        
        if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) {
            log_info("Plugin file changed: %s", event->name);
            plugin_loader_reload(path);
        } else if (event->mask & IN_DELETE) {
            log_info("Plugin file deleted: %s", event->name);
            loaded_plugin_t *plugin = find_plugin_by_path(path);
            if (plugin && plugin->loaded) {
                plugin_loader_unload(plugin->name);
            }
        }
    }
}

void plugin_loader_shutdown(void) {
    /* Stop watcher */
    if (g_loader.inotify_fd >= 0) {
        if (g_loader.watch_fd >= 0) {
            inotify_rm_watch(g_loader.inotify_fd, g_loader.watch_fd);
        }
        close(g_loader.inotify_fd);
        g_loader.inotify_fd = -1;
    }
    
    /* Unload all plugins */
    for (int i = 0; i < g_loader.plugin_count; i++) {
        if (g_loader.plugins[i].loaded) {
            plugin_loader_unload(g_loader.plugins[i].name);
        }
    }
    
    log_info("Plugin loader shutdown");
}

int plugin_loader_count(void) {
    int count = 0;
    for (int i = 0; i < g_loader.plugin_count; i++) {
        if (g_loader.plugins[i].loaded) count++;
    }
    return count;
}
