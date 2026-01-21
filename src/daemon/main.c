/*
 * main.c - Daemon entry point
 */
#include "daemon.h"
#include "config.h"
#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    qmem_config_t config;
    
    /* Initialize with defaults */
    config_init_defaults(&config);
    
    /* Parse command-line arguments */
    if (config_parse_args(&config, argc, argv) < 0) {
        config_print_usage(argv[0]);
        return 1;
    }
    
    /* Initialize logging */
    log_init(config.log_level, false, "qmemd");
    
    log_info("QMem Memory Monitor Daemon v1.0.0");
    
    /* Initialize daemon */
    if (daemon_init(&config) < 0) {
        log_error("Failed to initialize daemon");
        return 1;
    }
    
    /* Run main loop */
    int ret = daemon_run();
    
    /* Cleanup */
    daemon_shutdown();
    
    return ret;
}
