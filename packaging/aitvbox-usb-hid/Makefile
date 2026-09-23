include $(TOPDIR)/rules.mk

PKG_NAME:=aitvbox-usb-hid
PKG_VERSION:=1.0.0
PKG_RELEASE:=3

include $(INCLUDE_DIR)/package.mk

PKG_FILE_DEPENDS += ./files/aitvbox-hidctl
PKG_FILE_DEPENDS += ./files/aitvbox-usb-hid-test
PKG_FILE_DEPENDS += ./files/aitvbox-usb-hid.init

define Package/aitvbox-usb-hid
  SECTION:=utils
  CATEGORY:=Allwinner
  SUBMENU:=Custom
  TITLE:=AITVBox composite USB keyboard and mouse
  DEPENDS:=+adbd
endef

define Package/aitvbox-usb-hid/description
  Adds a production-default HID keyboard and mouse gadget. Authorized and
  development ADB modes remain explicit service-mode options.
endef

define Build/Compile
endef

define Package/aitvbox-usb-hid/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) ./files/aitvbox-hidctl $(1)/usr/bin/aitvbox-hidctl
	$(INSTALL_BIN) ./files/aitvbox-usb-hid-test $(1)/usr/bin/aitvbox-usb-hid-test
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/aitvbox-usb-hid.init $(1)/etc/init.d/aitvbox-usb-hid
endef

$(eval $(call BuildPackage,aitvbox-usb-hid))
