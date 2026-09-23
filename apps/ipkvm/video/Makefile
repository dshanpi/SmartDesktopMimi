SDK_ROOT ?= /home/ubuntu/A133-Tina5.0-v0.9
BUILD_DIR ?= $(CURDIR)/../build

CROSS_COMPILE := $(SDK_ROOT)/out/toolchain/gcc-linaro-5.3.1-2016.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++

KERNEL_DIR := $(SDK_ROOT)/kernel/linux-4.9
SDK_LIB_DIR := $(SDK_ROOT)/buildroot/package/auto/sdk_lib
SDK_INCLUDE := $(SDK_LIB_DIR)/include
CEDARX_INCLUDE := $(SDK_LIB_DIR)/cedarx/include/libcedarc/include
STAGING_LIB := $(SDK_ROOT)/out/a133/b6/openwrt/staging_dir/target/usr/lib
STAGING_ROOT_INCLUDE := $(SDK_ROOT)/out/a133/b6/openwrt/staging_dir/target/usr/include
STAGING_INCLUDE := $(SDK_ROOT)/out/a133/b6/openwrt/staging_dir/target/usr/include/allwinner/include
TARGET := $(BUILD_DIR)/aitvbox-kvm-video

CFLAGS := -O2 -pipe -Wall -Wextra -fPIC
CXXFLAGS := $(CFLAGS) -std=gnu++11
CPPFLAGS := -DMODULE_VERSION=\"V2.1.20220906\" \
	-I$(KERNEL_DIR)/arch/arm64/include \
	-I$(SDK_INCLUDE) -I$(SDK_INCLUDE)/storage -I$(SDK_INCLUDE)/disp2 \
	-I$(SDK_INCLUDE)/cutils -I$(SDK_INCLUDE)/utils \
	-I$(SDK_INCLUDE)/camera -I$(CEDARX_INCLUDE) \
	-I$(STAGING_ROOT_INCLUDE) -I$(STAGING_INCLUDE)
SDK_WARN := -Wno-unused-parameter -Wno-unused-variable -Wno-return-type \
	-Wno-parentheses
LDFLAGS := -L$(STAGING_LIB) -Wl,--allow-shlib-undefined
LDLIBS := -lvencoder -lvenc_base -lvenc_common -lvenc_h264 -lvenc_h265 \
	-lvenc_jpeg -lMemAdapter -lVE -lcdc_base -luapi -ljpeg -lpthread -ldl

OBJECT_DIR := $(BUILD_DIR)/video-obj
PATCH_DIR := $(CURDIR)/patches
PATCHED_ENCODER_SRC := $(OBJECT_DIR)/AWVidoeEncoder.cpp
OBJECTS := $(OBJECT_DIR)/main.o $(OBJECT_DIR)/sdklog.o \
	$(OBJECT_DIR)/sunxiMemInterface.o $(OBJECT_DIR)/DmaIon.o \
	$(OBJECT_DIR)/AWVidoeEncoder.o

.PHONY: all check clean

all: check $(TARGET)

check:
	@test -x "$(CXX)" || { echo "missing A133 toolchain: $(CXX)" >&2; exit 1; }
	@command -v patch >/dev/null || { echo "missing host patch command" >&2; exit 1; }
	@test -f "$(SDK_INCLUDE)/AWVideoEncoder.h" || { echo "missing A133 encoder headers" >&2; exit 1; }
	@test -f "$(STAGING_LIB)/libvencoder.so" || { echo "missing A133 encoder libraries" >&2; exit 1; }

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(OBJECT_DIR):
	mkdir -p $@

$(OBJECT_DIR)/main.o: main.cpp | $(OBJECT_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-unused-function -c -o $@ $<

$(OBJECT_DIR)/sdklog.o: $(SDK_LIB_DIR)/sdk_log/sdklog.c | $(OBJECT_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDK_WARN) -c -o $@ $<

$(OBJECT_DIR)/sunxiMemInterface.o: $(SDK_LIB_DIR)/sdk_memory/sunxiMemInterface.c | $(OBJECT_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDK_WARN) -c -o $@ $<

$(OBJECT_DIR)/DmaIon.o: $(SDK_LIB_DIR)/sdk_memory/DmaIon.cpp | $(OBJECT_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SDK_WARN) -c -o $@ $<

$(PATCHED_ENCODER_SRC): $(SDK_LIB_DIR)/sdk_encoder/AWVidoeEncoder.cpp \
		$(PATCH_DIR)/AWVidoeEncoder-aligned-input.patch | $(OBJECT_DIR)
	cp $< $@
	patch --silent $@ < $(PATCH_DIR)/AWVidoeEncoder-aligned-input.patch

$(OBJECT_DIR)/AWVidoeEncoder.o: $(PATCHED_ENCODER_SRC)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SDK_WARN) -c -o $@ $<

clean:
	rm -rf $(OBJECT_DIR) $(TARGET)
