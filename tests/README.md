# QMem Test Tools

## qmem_test_tool (User Space)

A simple utility to simulate system activity for testing QMem integration.

### Usage
```bash
./qmem_test_tool leak    # Simulates memory leak (malloc 1MB/s)
./qmem_test_tool net     # Simulates network traffic (UDP flood)
./qmem_test_tool proc    # Simulates process churn (fork/exit)
```

## qmem_test_kmod (Kernel Space)

A kernel module that allocates ~64MB of kernel memory (Slab + SKB) to simulate leaks/growth.

### Build and Run
Requires kernel headers installed.

1. Build:
   ```bash
   make
   ```

2. Load (Leak Start):
   ```bash
   sudo insmod qmem_test_kmod.ko
   ```
   Check `dmesg` for allocation details.
   QMem `slabinfo` should show growth in `kmalloc-4k`, `skbuff_head_cache`, etc.

3. Unload (Cleanup):
   ```bash
   sudo rmmod qmem_test_kmod
   ```
