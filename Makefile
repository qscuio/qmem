# QMem - Linux Memory Monitor Daemon
# Main Makefile

CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -Werror -std=c11 -I./include -I./src
LDFLAGS := -lpthread -ldl

# Build type
DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0 -DDEBUG
    LDFLAGS += -g
else
    CFLAGS += -O2 -DNDEBUG
endif

# Enable web frontend
WEB ?= 1
ifeq ($(WEB), 1)
    CFLAGS += -DQMEM_WEB_ENABLED
endif

# Directories
SRCDIR := src
BUILDDIR := build
BINDIR := bin
PLUGINDIR := plugins

# Source files
COMMON_SRCS := $(wildcard $(SRCDIR)/common/*.c)
SERVICE_SRCS := $(wildcard $(SRCDIR)/services/*.c)
DAEMON_SRCS := $(wildcard $(SRCDIR)/daemon/*.c)
CLI_SRCS := $(wildcard $(SRCDIR)/cli/*.c)
WEB_SRCS := $(wildcard $(SRCDIR)/web/*.c)

# Object files
COMMON_OBJS := $(BUILDDIR)/common/format.o $(BUILDDIR)/common/json.o $(BUILDDIR)/common/log.o $(BUILDDIR)/common/proc_utils.o
SERVICE_OBJS := $(SERVICE_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
DAEMON_OBJS := $(BUILDDIR)/daemon/config.o $(BUILDDIR)/daemon/daemon.o $(BUILDDIR)/daemon/ipc_server.o $(BUILDDIR)/daemon/main.o $(BUILDDIR)/daemon/plugin_loader.o $(BUILDDIR)/daemon/ringbuffer.o $(BUILDDIR)/daemon/service_manager.o $(BUILDDIR)/web/api.o $(BUILDDIR)/web/http_server.o $(BUILDDIR)/web/static_files.o
CLI_OBJS := $(BUILDDIR)/cli/client.o $(BUILDDIR)/cli/commands.o $(BUILDDIR)/cli/main.o
WEB_OBJS := $(WEB_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Plugin shared libraries
PLUGIN_TARGETS := $(patsubst $(SRCDIR)/services/%.c,$(PLUGINDIR)/%.so,$(SERVICE_SRCS))

# Targets
DAEMON := $(BINDIR)/qmemd
CLI := $(BINDIR)/qmemctl
QMEM_TEST_TOOL := $(BINDIR)/qmem_test_tool

.PHONY: all clean install test dirs plugins

all: $(BINDIR)/qmemd $(BINDIR)/qmemctl $(BINDIR)/qmem_test_tool plugins installer_bin

# Daemon objects (include web if enabled, services linked statically for now)
DAEMON_ALL_OBJS := $(DAEMON_OBJS) $(COMMON_OBJS)
ifeq ($(WEB), 1)
    DAEMON_ALL_OBJS += $(WEB_OBJS)
endif



# Build with plugins as .so files
plugins: dirs plugin-dirs $(PLUGIN_TARGETS)

plugin-dirs:
	@mkdir -p $(PLUGINDIR)

dirs:
	@mkdir -p $(BUILDDIR)/common
	@mkdir -p $(BUILDDIR)/services
	@mkdir -p $(BUILDDIR)/daemon
	@mkdir -p $(BUILDDIR)/cli
	@mkdir -p $(BUILDDIR)/web
	@mkdir -p $(BINDIR)

$(DAEMON): $(DAEMON_ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BINDIR)/qmemctl: $(CLI_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BINDIR)/qmem_test_tool: tests/qmem_test_tool.c | dirs
	$(CC) $(CFLAGS) -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | dirs
	$(CC) $(CFLAGS) -c -o $@ $<

# Plugin build rule - compile service as shared library
# Plugin build rule - compile service as shared library
$(PLUGINDIR)/%.so: $(SRCDIR)/services/%.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< $(COMMON_OBJS) $(LDFLAGS)

# Procmem object without plugin define for static linking into other plugins
$(BUILDDIR)/services/procmem_noplugin.o: $(SRCDIR)/services/procmem.c | dirs
	$(CC) $(CFLAGS) -DNO_PLUGIN_DEFINE -fPIC -c -o $@ $<

# Slabinfo object without plugin define
$(BUILDDIR)/services/slabinfo_noplugin.o: $(SRCDIR)/services/slabinfo.c | dirs
	$(CC) $(CFLAGS) -DNO_PLUGIN_DEFINE -fPIC -c -o $@ $<

# Heapmon object without plugin define
$(BUILDDIR)/services/heapmon_noplugin.o: $(SRCDIR)/services/heapmon.c | dirs
	$(CC) $(CFLAGS) -DNO_PLUGIN_DEFINE -fPIC -c -o $@ $<

# Meminfo object without plugin define
$(BUILDDIR)/services/meminfo_noplugin.o: $(SRCDIR)/services/meminfo.c | dirs
	$(CC) $(CFLAGS) -DNO_PLUGIN_DEFINE -fPIC -c -o $@ $<

# Heapmon needs procmem functions
$(PLUGINDIR)/heapmon.so: $(SRCDIR)/services/heapmon.c $(BUILDDIR)/services/procmem_noplugin.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $^ $(LDFLAGS)

# Memleak needs procmem, slabinfo, heapmon, meminfo
$(PLUGINDIR)/memleak.so: $(SRCDIR)/services/memleak.c $(BUILDDIR)/services/procmem_noplugin.o $(BUILDDIR)/services/slabinfo_noplugin.o $(BUILDDIR)/services/heapmon_noplugin.o $(BUILDDIR)/services/meminfo_noplugin.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR) $(BINDIR) $(PLUGINDIR)
	rm -f qmem_install_info.sh qmem_install_debug.sh qmem_install_release.sh

install: all plugins
	install -d /usr/local/bin
	install -m 755 $(DAEMON) /usr/local/bin/
	install -m 755 $(CLI) /usr/local/bin/
	install -d /usr/lib/qmem/plugins
	install -m 755 $(PLUGINDIR)/*.so /usr/lib/qmem/plugins/ 2>/dev/null || true
	install -d /etc/qmem
	install -m 644 config/qmem.conf.example /etc/qmem/

test: all
	@echo "Running tests..."
	@$(MAKE) -C tests

# Build Debian packages
deb:
	dpkg-buildpackage -us -uc -b

# Build self-extracting installer
installer:
# Package the current build as an installer
installer_bin:
	@echo "Packaging installer for current build (DEBUG=$(DEBUG))..."
	./scripts/make_installer.sh $(if $(filter 1,$(DEBUG)),debug,release)

# Build BOTH installers (Release then Debug) and preserve them in bin/
installer:
	@echo "Building Release Installer..."
	$(MAKE) clean
	$(MAKE) all DEBUG=0
	@# Save release installer
	@cp $(BINDIR)/qmem_install_release.sh /tmp/qmem_install_release.sh
	@echo "Building Debug Installer..."
	$(MAKE) clean
	$(MAKE) all DEBUG=1
	@# Restore release installer
	@mv /tmp/qmem_install_release.sh $(BINDIR)/
	@echo "Installers generated in $(BINDIR)/:"
	@ls -l $(BINDIR)/qmem_install_*.sh

# Dependencies (auto-generated)
-include $(BUILDDIR)/**/*.d


