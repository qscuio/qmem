/*
 * qmem_test_kmod.c - Kernel memory leak simulator
 * Allocates up to 64MB of memory (kmalloc + skb) to test slab monitoring.
 * Note: This "leaks" by holding references until unload.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("QMem Tester");
MODULE_DESCRIPTION("Simulates kernel memory usage/leaks");

#define TOTAL_LIMIT_MB 64
#define CHUNK_SIZE 4096

struct leak_node {
    struct list_head list;
    void *ptr;
    struct sk_buff *skb;
};

static LIST_HEAD(leak_list);
static long total_allocated = 0;

static int __init qmem_test_init(void) {
    int i;
    long target_bytes = TOTAL_LIMIT_MB * 1024 * 1024;
    
    printk(KERN_INFO "qmem_test_kmod: Initializing leak simulation (target %d MB)...\n", TOTAL_LIMIT_MB);

    /* 1. Kmalloc allocations (32MB) */
    for (i = 0; i < (target_bytes / 2) / CHUNK_SIZE; i++) {
        struct leak_node *node;
        void *mem;
        
        node = kmalloc(sizeof(*node), GFP_KERNEL);
        if (!node) break;
        
        mem = kmalloc(CHUNK_SIZE, GFP_KERNEL);
        if (!mem) {
            kfree(node);
            break;
        }
        
        /* Touch memory */
        memset(mem, 0xBB, CHUNK_SIZE);
        
        node->ptr = mem;
        node->skb = NULL;
        list_add(&node->list, &leak_list);
        total_allocated += CHUNK_SIZE;
    }

    /* 2. SKB allocations (32MB) */
    /* Approx 2KB per SKB (data + overhead) */
    for (i = 0; i < (target_bytes / 2) / 2048; i++) {
        struct leak_node *node;
        struct sk_buff *skb;
        
        node = kmalloc(sizeof(*node), GFP_KERNEL);
        if (!node) break;
        
        skb = alloc_skb(1500, GFP_KERNEL);
        if (!skb) {
            kfree(node);
            break;
        }
        
        node->ptr = NULL;
        node->skb = skb;
        list_add(&node->list, &leak_list);
        total_allocated += 1500 + sizeof(struct sk_buff); /* Roughly */
    }

    printk(KERN_INFO "qmem_test_kmod: Allocated ~%ld MB\n", total_allocated / (1024*1024));
    return 0;
}

static void __exit qmem_test_exit(void) {
    struct leak_node *node, *tmp;
    
    printk(KERN_INFO "qmem_test_kmod: Cleaning up...\n");
    
    list_for_each_entry_safe(node, tmp, &leak_list, list) {
        if (node->ptr) kfree(node->ptr);
        if (node->skb) kfree_skb(node->skb);
        list_del(&node->list);
        kfree(node);
    }
    
    printk(KERN_INFO "qmem_test_kmod: Create/Delete complete.\n");
}

module_init(qmem_test_init);
module_exit(qmem_test_exit);
