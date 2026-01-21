/*
 * main.c - CLI entry point (qmemctl)
 */
#include "commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#define DEFAULT_SOCKET "/run/qmem.sock"

static void print_usage(const char *prog) {
    printf("Usage: %s [options] <command>\n\n", prog);
    printf("Commands:\n");
    printf("  status [svc] Show current memory status (or specific service)\n");
    printf("  top       Show top memory consumers/growers\n");
    printf("  slab      Show slab cache changes\n");
    printf("  sockets   Show detailed socket connections\n");
    printf("  watch     Continuously monitor (like top)\n");
    printf("            Usage: watch [list]\n");
    printf("  raw       Dump raw JSON snapshot\n");
    printf("\nOptions:\n");
    printf("  -s, --socket PATH   Unix socket path (default: %s)\n", DEFAULT_SOCKET);
    printf("  -i, --interval SEC  Watch interval in seconds (default: 2)\n");
    printf("  -h, --help          Show this help\n");
}

int main(int argc, char **argv) {
    const char *socket_path = DEFAULT_SOCKET;
    int interval = 2;
    
    static struct option long_options[] = {
        {"socket",   required_argument, 0, 's'},
        {"interval", required_argument, 0, 'i'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "s:i:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                socket_path = optarg;
                break;
            case 'i':
                interval = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Check for root privileges */
    if (geteuid() != 0) {
        fprintf(stderr, "Error: qmemctl must be run as root\n");
        return 1;
    }
    
    if (optind >= argc) {
        /* No command, default to status */
        return cmd_status(socket_path, NULL);
    }
    
    const char *command = argv[optind];
    
    if (strcmp(command, "status") == 0) {
        const char *target = NULL;
        if (optind + 1 < argc) target = argv[optind + 1];
        return cmd_status(socket_path, target);
    } else if (strcmp(command, "top") == 0) {
        return cmd_top(socket_path);
    } else if (strcmp(command, "slab") == 0) {
        return cmd_slab(socket_path);
    } else if (strcmp(command, "sockets") == 0) {
        return cmd_sockets(socket_path);
    } else if (strcmp(command, "watch") == 0) {
        /* Check for subcommand 'list' */
        if (optind + 1 < argc && strcmp(argv[optind + 1], "list") == 0) {
            return cmd_services(socket_path);
        }
        return cmd_watch(socket_path, interval);
    } else if (strcmp(command, "raw") == 0) {
        return cmd_raw(socket_path);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }
}
