include $(TOPDIR)/rules.mk

PKG_NAME:=aitvbox-ipkvm
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

ifeq ($(origin AITVBOX_PRODUCT_BUILD),undefined)
$(error AITVBOX_PRODUCT_BUILD is not set. Run via scripts/build_firmware.sh.)
endif

PRODUCT_ROOT := $(abspath $(AITVBOX_PRODUCT_BUILD))
BUILD_OUT := $(PRODUCT_ROOT)/build/ipkvm

PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-ipkvmd
PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-kvm-video
PKG_FILE_DEPENDS += ./files/aitvbox-ipkvm.init
PKG_FILE_DEPENDS += ./files/aitvbox-ipkvmctl

define Package/aitvbox-ipkvm
  SECTION:=utils
  CATEGORY:=Allwinner
  SUBMENU:=Custom
  TITLE:=AITVBox browser IPKVM service
  DEPENDS:=+aitvbox-usb-hid +frpc +libstdcpp +libcedarx +libuapi +libatomic +libjpeg-turbo
endef

define Package/aitvbox-ipkvm/description
  Installs the authenticated WebRTC browser KVM server and the A133 hardware
  H.264 HDMI capture sender. USB HID uses the shared AITVBox gadget.
endef

define Build/Prepare
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/aitvbox-ipkvm/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(BUILD_OUT)/aitvbox-ipkvmd $(1)/usr/bin/aitvbox-ipkvmd
	$(INSTALL_BIN) $(BUILD_OUT)/aitvbox-kvm-video $(1)/usr/bin/aitvbox-kvm-video
	ln -sf aitvbox-kvm-video $(1)/usr/bin/aitvbox-captured
	$(INSTALL_BIN) ./files/aitvbox-ipkvmctl $(1)/usr/bin/aitvbox-ipkvmctl

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/aitvbox-ipkvm.init $(1)/etc/init.d/aitvbox-ipkvm
	$(INSTALL_DIR) $(1)/etc/rc.d
	ln -sf ../init.d/aitvbox-ipkvm $(1)/etc/rc.d/S98aitvbox-ipkvm
endef

$(eval $(call BuildPackage,aitvbox-ipkvm))
