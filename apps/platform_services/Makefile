APPS := aitvbox-mcpd aitvbox-agentd aitvbox-controld aitvbox-appd aitvbox-app-policy
PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SAFE_IO_SOURCE := src/safe_io.c
SDK_ROOT ?= $(abspath $(PROJECT_DIR)/../..)
TARGET_SYSROOT ?= $(SDK_ROOT)/out/a133/b6/openwrt/staging_dir/target
OPENWRT_CONFIG ?= $(SDK_ROOT)/out/a133/b6/openwrt/tmp/.config
export STAGING_DIR := $(SDK_ROOT)/out/a133/b6/openwrt/staging_dir
-include $(OPENWRT_CONFIG)

CONFIG_TOOLCHAIN_ROOT_CLEAN := $(patsubst "%",%,$(CONFIG_TOOLCHAIN_ROOT))
TOOLCHAIN_PREFIX := $(patsubst "%",%,$(CONFIG_TOOLCHAIN_PREFIX))
TOOLCHAIN_ROOT := $(if $(wildcard $(CONFIG_TOOLCHAIN_ROOT_CLEAN)),$(CONFIG_TOOLCHAIN_ROOT_CLEAN),$(SDK_ROOT)$(CONFIG_TOOLCHAIN_ROOT_CLEAN))
CROSS_COMPILE ?= $(TOOLCHAIN_ROOT)/bin/$(TOOLCHAIN_PREFIX)
CC := $(CROSS_COMPILE)gcc
BUILD_DIR ?= $(PROJECT_DIR)/build
TARGETS := $(addprefix $(BUILD_DIR)/,$(APPS))

CPPFLAGS += -I$(TARGET_SYSROOT)/usr/include
CFLAGS += -std=gnu99 -O2 -Wall -Wextra -Werror
LDFLAGS += -L$(TARGET_SYSROOT)/usr/lib \
	-Wl,-rpath-link,$(TARGET_SYSROOT)/lib \
	-Wl,-rpath-link,$(TARGET_SYSROOT)/usr/lib \
	-Wl,-rpath-link,$(TARGET_SYSROOT)/root-a133-b6/lib \
	-Wl,-rpath-link,$(TARGET_SYSROOT)/root-a133-b6/usr/lib
LDLIBS += -ljson-c

.PHONY: all clean install
all: $(TARGETS)

$(BUILD_DIR)/aitvbox-mcpd: src/mcpd.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/aitvbox-agentd: src/agentd.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS) -lcurl

$(BUILD_DIR)/aitvbox-controld: src/controld.c $(SAFE_IO_SOURCE) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/aitvbox-appd: src/appd.c $(SAFE_IO_SOURCE) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/aitvbox-app-policy: src/app_policy.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $@

install: $(TARGETS)
	install -D -m 0755 $(BUILD_DIR)/aitvbox-mcpd $(DESTDIR)/usr/bin/aitvbox-mcpd
	install -D -m 0755 $(BUILD_DIR)/aitvbox-agentd $(DESTDIR)/usr/bin/aitvbox-agentd
	install -D -m 0755 $(BUILD_DIR)/aitvbox-controld $(DESTDIR)/usr/bin/aitvbox-controld
	install -D -m 0755 $(BUILD_DIR)/aitvbox-appd $(DESTDIR)/usr/bin/aitvbox-appd
	install -D -m 0755 $(BUILD_DIR)/aitvbox-app-policy $(DESTDIR)/usr/bin/aitvbox-app-policy

clean:
	rm -rf $(BUILD_DIR)
