/*
 * qmem_test_tool.c - Simple test program for QMem
 * Simulates memory leaks, network traffic, and process churn.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>

static volatile int running = 1;

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

void handle_sig(int sig) {
    (void)sig;
    running = 0;
}

void do_leak(void) {
    printf("Starting Memory Leak Simulation...\n");
    printf("- Allocating memory every 1 second\n");
    printf("- Uses BOTH heap (small malloc) AND mmap (large anonymous)\n");
    printf("- Memory is TOUCHED to ensure RSS growth\n");
    printf("- Press Ctrl+C to stop\n\n");
    
    long total_heap = 0;
    long total_mmap = 0;
    int iteration = 0;
    
    while(running) {
        iteration++;
        
        /* Alternate between heap and mmap allocations */
        if (iteration % 2 == 1) {
            /* Small malloc allocations - uses heap (brk/sbrk) */
            /* Allocate 16 x 64KB = 1MB total, but in small chunks */
            for (int i = 0; i < 16 && running; i++) {
                size_t size = 64 * 1024; /* 64KB - stays on heap */
                char *p = malloc(size);
                if (!p) {
                    printf("Malloc failed!\n");
                    break;
                }
                memset(p, 0xAA, size);
                total_heap += size;
            }
            printf("[HEAP] Leaked 1MB via malloc. Heap Total: %ld MB\n", 
                   total_heap / (1024*1024));
        } else {
            /* Large mmap allocation - explicit anonymous mapping */
            size_t size = 1024 * 1024; /* 1MB */
            void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) {
                printf("Mmap failed!\n");
            } else {
                memset(p, 0xBB, size);
                total_mmap += size;
                printf("[MMAP] Leaked 1MB via mmap. Mmap Total: %ld MB\n",
                       total_mmap / (1024*1024));
            }
        }
        
        printf("       Total leaked: %ld MB (Heap: %ld MB, Mmap: %ld MB)\n",
               (total_heap + total_mmap) / (1024*1024),
               total_heap / (1024*1024), total_mmap / (1024*1024));
        
        sleep(1);
    }
}

void do_network(void) {
    printf("Starting Interface Statistics Simulation...\n");
    printf("- Sending UDP packets to 127.0.0.1:12345\n");
    printf("- Generates TX (and RX if loopback monitored) traffic\n");
    printf("- Press Ctrl+C to stop\n\n");

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    char buf[1024];
    memset(buf, 'X', sizeof(buf));
    
    long count = 0;
    while(running) {
        sendto(sock, buf, sizeof(buf), 0, (struct sockaddr*)&addr, sizeof(addr));
        count++;
        if (count % 1000 == 0) {
            printf("Sent %ld packets (%ld MB)\n", count, (count * sizeof(buf)) / (1024*1024));
            sleep_ms(100); /* 100ms pause every 1000 packets */
        }
        sleep_ms(1); /* Throttling, 1ms */
    }
    close(sock);
}

void do_fork(void) {
    printf("Starting Process Create/Delete Simulation...\n");
    printf("- Forking children every 500ms\n");
    printf("- Generates 'fork' and 'exit' events for procevent\n");
    printf("- Press Ctrl+C to stop\n\n");
    
    while(running) {
        pid_t pid = fork();
        if (pid == 0) {
            /* Child */
            sleep_ms(100); /* Live for 100ms */
            exit(0);
        } else if (pid > 0) {
            /* Parent */
            printf("Forked child PID: %d\n", pid);
            wait(NULL); /* Wait for it to die (prevent zombies) */
        }
        
        sleep_ms(500); /* 500ms interval */
    }
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);
    
    if (argc < 2) {
        printf("Usage: %s <mode>\n\n", argv[0]);
        printf("Modes:\n");
        printf("  leak     Simulate memory leak (malloc 1MB/s)\n");
        printf("  net      Simulate network traffic (UDP flood)\n");
        printf("  proc     Simulate process creation/deletion (fork loop)\n");
        return 1;
    }
    
    if (strcmp(argv[1], "leak") == 0) do_leak();
    else if (strcmp(argv[1], "net") == 0) do_network();
    else if (strcmp(argv[1], "proc") == 0) do_fork();
    else {
        printf("Unknown mode: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}
