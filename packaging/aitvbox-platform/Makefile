include $(TOPDIR)/rules.mk

PKG_NAME:=aitvbox-platform
PKG_VERSION:=1.0.0
PKG_RELEASE:=3

include $(INCLUDE_DIR)/package.mk

ifeq ($(origin AITVBOX_PRODUCT_BUILD),undefined)
$(error AITVBOX_PRODUCT_BUILD is not set. Use the product build scripts.)
endif
PRODUCT_ROOT := $(abspath $(AITVBOX_PRODUCT_BUILD))
BUILD_OUT := $(PRODUCT_ROOT)/build/platform

PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-mcpd
PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-agentd
PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-controld
PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-appd
PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-app-policy
PKG_FILE_DEPENDS += $(PRODUCT_ROOT)/platform/runtime/aitvbox-appctl
PKG_FILE_DEPENDS += $(PRODUCT_ROOT)/platform/runtime/aitvbox-agentctl
PKG_FILE_DEPENDS += $(PRODUCT_ROOT)/platform/runtime/aitvbox-adminctl

define Package/aitvbox-platform
  SECTION:=utils
  CATEGORY:=Allwinner
  SUBMENU:=Custom
  TITLE:=AITVBox trusted app, MCP, agent, and hardware platform
  DEPENDS:=+aitvbox-usb-hid +libjson-c +libcurl +jsonfilter +openssl-util +coreutils +coreutils-od
endef

define Package/aitvbox-platform/description
  Installs the isolated trusted application runtime, MCP computer-control
  server, autonomous visual agent, and their security configuration tools.
endef

define Build/Prepare
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/aitvbox-platform/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(BUILD_OUT)/aitvbox-mcpd $(1)/usr/bin/aitvbox-mcpd
	$(INSTALL_BIN) $(BUILD_OUT)/aitvbox-agentd $(1)/usr/bin/aitvbox-agentd
	$(INSTALL_BIN) $(BUILD_OUT)/aitvbox-controld $(1)/usr/bin/aitvbox-controld
	$(INSTALL_BIN) $(BUILD_OUT)/aitvbox-appd $(1)/usr/bin/aitvbox-appd
	$(INSTALL_BIN) $(BUILD_OUT)/aitvbox-app-policy $(1)/usr/bin/aitvbox-app-policy
	$(INSTALL_BIN) $(PRODUCT_ROOT)/platform/runtime/aitvbox-appctl $(1)/usr/bin/aitvbox-appctl
	$(INSTALL_BIN) $(PRODUCT_ROOT)/platform/runtime/aitvbox-agentctl $(1)/usr/bin/aitvbox-agentctl
	$(INSTALL_BIN) $(PRODUCT_ROOT)/platform/runtime/aitvbox-adminctl $(1)/usr/bin/aitvbox-adminctl
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/aitvbox-control.init $(1)/etc/init.d/aitvbox-control
	$(INSTALL_BIN) ./files/aitvbox-apps.init $(1)/etc/init.d/aitvbox-apps
endef

$(eval $(call BuildPackage,aitvbox-platform))
