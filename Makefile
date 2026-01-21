# QMem - Linux Memory Monitor Daemon
# Main Makefile

CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -std=c11 -I./include -I./src
LDFLAGS := -lpthread -ldl

# Build type
DEBUG ?= 0
ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0 -DDEBUG -fsanitize=address
    LDFLAGS += -fsanitize=address
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
COMMON_OBJS := $(COMMON_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
SERVICE_OBJS := $(SERVICE_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
DAEMON_OBJS := $(DAEMON_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
CLI_OBJS := $(CLI_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
WEB_OBJS := $(WEB_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Plugin shared libraries
PLUGIN_TARGETS := $(patsubst $(SRCDIR)/services/%.c,$(PLUGINDIR)/%.so,$(SERVICE_SRCS))

# Targets
DAEMON := $(BINDIR)/qmemd
CLI := $(BINDIR)/qmemctl

# Daemon objects (include web if enabled, services linked statically for now)
DAEMON_ALL_OBJS := $(DAEMON_OBJS) $(COMMON_OBJS)
ifeq ($(WEB), 1)
    DAEMON_ALL_OBJS += $(WEB_OBJS)
endif

.PHONY: all clean install test dirs plugins

all: dirs $(DAEMON) $(CLI) plugins installer

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

$(CLI): $(CLI_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Plugin build rule - compile service as shared library
# Plugin build rule - compile service as shared library
$(PLUGINDIR)/%.so: $(SRCDIR)/services/%.c $(COMMON_OBJS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< $(COMMON_OBJS) $(LDFLAGS)

# Procmem object without plugin define for static linking into other plugins
$(BUILDDIR)/services/procmem_noplugin.o: $(SRCDIR)/services/procmem.c
	$(CC) $(CFLAGS) -DNO_PLUGIN_DEFINE -fPIC -c -o $@ $<

# Heapmon needs procmem functions
$(PLUGINDIR)/heapmon.so: $(SRCDIR)/services/heapmon.c $(BUILDDIR)/services/procmem_noplugin.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR) $(BINDIR) $(PLUGINDIR)

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
installer: dirs $(DAEMON) $(CLI) plugins
	./scripts/make_installer.sh

# Dependencies (auto-generated)
-include $(BUILDDIR)/**/*.d


