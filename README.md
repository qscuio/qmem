# QMem - Linux System Monitor Daemon

A modular, extensible Linux daemon in C that monitors system resources including memory, CPU, network, processes, and sockets. Features dynamic plugin loading with hot-reload, REST API, web dashboard, and CLI.

## Features

### Monitoring Services (10)

| Service | Description |
|---------|-------------|
| **meminfo** | System memory (Total, Available, Free, Cached, Buffers) |
| **slabinfo** | Kernel slab cache growers/shrinkers |
| **procmem** | Per-process RSS and VmData tracking |
| **heapmon** | Heap region analysis via /proc/pid/smaps |
| **vmstat** | Kernel VM statistics |
| **cpuload** | Per-process CPU usage percentage |
| **netstat** | Network interface RX/TX bytes, packets, rates |
| **procstat** | Process states (Running/Sleeping/Blocked) with wait channels |
| **sockstat** | Socket statistics (TCP states, UDP, Unix) |
| **procevent** | Process fork/exit event detection |

### Dynamic Plugin System

- Services compiled as `.so` shared libraries
- Hot-reload via `inotify` file watching
- Drop plugins in `/usr/lib/qmem/plugins/` to auto-load

### Interfaces

- **REST API** - `/api/status`, `/api/health`, `/api/snapshot`
- **Web Dashboard** - Modern dark-themed SPA on port 8080
- **CLI** - `qmemctl` with status, top, slab, watch commands
- **IPC** - Unix socket at `/run/qmem.sock`

## Building

```bash
# Build daemon and CLI
make

# Build plugins as .so files
make plugins

# Build with debug/ASan
make DEBUG=1

# Build without web frontend
make WEB=0

# Build Debian packages
sudo apt install debhelper dpkg-dev
make deb
```

## Installation

```bash
# Manual install
sudo make install

# Or install .deb packages
sudo dpkg -i ../qmem_1.0.0-1_*.deb ../qmem-plugins_1.0.0-1_*.deb

# Enable systemd service
sudo systemctl enable --now qmem
```

## Usage

### Daemon

```bash
# Run in foreground
qmemd -f

# Run with custom config
qmemd -c /etc/qmem/qmem.conf

# Run with custom interval and port
qmemd -i 5 -p 8080

# Debug mode (verbose logging)
qmemd -f -d
```

### CLI

```bash
# Show current status
qmemctl status

# Watch memory changes (live updates)
qmemctl watch

# Show top memory consumers
qmemctl top

# Show slab cache info
qmemctl slab

# Raw JSON output
qmemctl raw
```

### Web Interface

Access the dashboard at `http://localhost:8080` when the daemon is running.

### REST API

```bash
curl http://localhost:8080/api/health
curl http://localhost:8080/api/status
```

## Architecture

```
qmemd (daemon)
  ├── Plugin Loader (dlopen, inotify)
  ├── Service Manager
  │   ├── meminfo   - /proc/meminfo
  │   ├── slabinfo  - /proc/slabinfo (needs root)
  │   ├── procmem   - Per-process memory
  │   ├── heapmon   - Heap analysis
  │   ├── vmstat    - /proc/vmstat
  │   ├── cpuload   - Per-process CPU
  │   ├── netstat   - /proc/net/dev
  │   ├── procstat  - Process states
  │   ├── sockstat  - Socket statistics
  │   └── procevent - Fork/exit events
  ├── IPC Server (Unix socket)
  ├── HTTP Server + REST API
  └── Ring Buffer (history)

qmemctl (CLI)
  └── IPC Client → qmemd

plugins/*.so
  └── Dynamically loaded services
```

## Configuration

See `config/qmem.conf.example` for all options:

```ini
[daemon]
interval = 10
foreground = false
socket = /run/qmem.sock
log_level = info

[web]
enabled = true
listen = 0.0.0.0
port = 8080

[plugins]
enabled = true
dir = /usr/lib/qmem/plugins
```

## Creating Plugins

```c
#include <qmem/plugin.h>

static qmem_service_ops_t my_ops = { .init, .collect, .snapshot, .destroy };
qmem_service_t my_service = { "myservice", "Description", &my_ops };

QMEM_PLUGIN_DEFINE("myservice", "1.0", "My custom service", my_service);
```

Build and install:
```bash
gcc -fPIC -shared -o myservice.so myservice.c -I/path/to/qmem/include
sudo cp myservice.so /usr/lib/qmem/plugins/
# Daemon will auto-load via inotify
```

## License

MIT
