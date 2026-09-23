APP := hdmi_preview

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SDK_ROOT ?= $(abspath $(PROJECT_DIR)/..)

OUT_DIR ?= $(SDK_ROOT)/out/a133/b6/openwrt
OPENWRT_STAGING_DIR ?= $(OUT_DIR)/staging_dir
TARGET_SYSROOT ?= $(OPENWRT_STAGING_DIR)/target
OPENWRT_CONFIG ?= $(SDK_ROOT)/openwrt/openwrt/.config

ifeq ($(wildcard $(OPENWRT_CONFIG)),)
OPENWRT_CONFIG := $(OUT_DIR)/tmp/.config
endif

LICHEE_TOP_DIR ?= $(SDK_ROOT)

-include $(OPENWRT_CONFIG)

CONFIG_TOOLCHAIN_ROOT_CLEAN := $(patsubst "%",%,$(CONFIG_TOOLCHAIN_ROOT))
CONFIG_TOOLCHAIN_PREFIX_CLEAN := $(patsubst "%",%,$(CONFIG_TOOLCHAIN_PREFIX))

TOOLCHAIN_DIR ?= $(if $(CONFIG_TOOLCHAIN_ROOT_CLEAN),$(CONFIG_TOOLCHAIN_ROOT_CLEAN),$(SDK_ROOT)/prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-1130/toolchain)
TOOLCHAIN_PREFIX ?= $(if $(CONFIG_TOOLCHAIN_PREFIX_CLEAN),$(CONFIG_TOOLCHAIN_PREFIX_CLEAN),aarch64-openwrt-linux-)
CROSS_COMPILE ?= $(TOOLCHAIN_DIR)/bin/$(TOOLCHAIN_PREFIX)

ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CXX),default)
CXX := $(CROSS_COMPILE)g++
endif

BUILD_DIR ?= $(PROJECT_DIR)/build
SRC_DIR := $(PROJECT_DIR)/src

export STAGING_DIR := $(OPENWRT_STAGING_DIR)

SRCS := $(SRC_DIR)/main.cpp
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
TARGET := $(BUILD_DIR)/$(APP)

CPPFLAGS += \
	-I$(SRC_DIR) \
	-I$(TARGET_SYSROOT)/usr/include \
	-I$(TARGET_SYSROOT)/usr/include/allwinner/include \
	-I$(TARGET_SYSROOT)/usr/include/allwinner

ifeq ($(CONFIG_SUNXI_ALLOC_CMA),y)
CPPFLAGS += -DCONF_KERNEL_CMA
endif
ifeq ($(CONFIG_SUNXI_ALLOC_IOMMU),y)
CPPFLAGS += -DCONF_KERNEL_IOMMU
endif

CXXFLAGS += -std=gnu++11 -O2 -fPIC -Wall -Wextra -Wno-format-security
LDFLAGS += \
	-L$(TARGET_SYSROOT)/usr/lib \
	-Wl,-rpath-link,$(TARGET_SYSROOT)/lib \
	-Wl,-rpath-link,$(TARGET_SYSROOT)/usr/lib \
	-Wl,-rpath-link,$(TARGET_SYSROOT)/root-a133-b6/lib \
	-Wl,-rpath-link,$(TARGET_SYSROOT)/root-a133-b6/usr/lib
LDLIBS += -luapi -ljpeg -lasound -lsamplerate -lpthread -ldl -lm

.PHONY: all clean install print-config

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

install: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)/usr/bin/$(APP)

print-config:
	@echo "APP=$(APP)"
	@echo "SDK_ROOT=$(SDK_ROOT)"
	@echo "OPENWRT_STAGING_DIR=$(OPENWRT_STAGING_DIR)"
	@echo "TARGET_SYSROOT=$(TARGET_SYSROOT)"
	@echo "OPENWRT_CONFIG=$(OPENWRT_CONFIG)"
	@echo "CONFIG_SUNXI_ALLOC_CMA=$(CONFIG_SUNXI_ALLOC_CMA)"
	@echo "CONFIG_SUNXI_ALLOC_IOMMU=$(CONFIG_SUNXI_ALLOC_IOMMU)"
	@echo "TOOLCHAIN_DIR=$(TOOLCHAIN_DIR)"
	@echo "TOOLCHAIN_PREFIX=$(TOOLCHAIN_PREFIX)"
	@echo "CROSS_COMPILE=$(CROSS_COMPILE)"
	@echo "CC=$(CC)"
	@echo "CXX=$(CXX)"
	@echo "TARGET=$(TARGET)"

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
