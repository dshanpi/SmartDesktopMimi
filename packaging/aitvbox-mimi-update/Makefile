include $(TOPDIR)/rules.mk
PKG_NAME:=aitvbox-mimi-update
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
include $(INCLUDE_DIR)/package.mk
ifeq ($(origin AITVBOX_PRODUCT_BUILD),undefined)
$(error AITVBOX_PRODUCT_BUILD is required)
endif
PRODUCT_ROOT:=$(abspath $(AITVBOX_PRODUCT_BUILD))
# Factory executables are already covered by signed file hashes. Never strip
# them after signing; size reduction belongs before application packaging.
RSTRIP:=:
PKG_FILE_DEPENDS += $(PRODUCT_ROOT)/build/mimi-update/mimi-update
PKG_FILE_DEPENDS += $(PRODUCT_ROOT)/build/mimi-baseline
PKG_FILE_DEPENDS += ./files
define Package/aitvbox-mimi-update
  SECTION:=utils
  CATEGORY:=Allwinner
  TITLE:=SmartDesktopMimi independent application updater
  # The signed factory desktop is shipped in this package and needs these
  # platform libraries before it can start (the Go updater itself is static).
  DEPENDS:=+ca-bundle +alsa-lib +libevdev +libjson-c +libstdcpp +libcurl
endef
define Build/Prepare
endef
define Build/Configure
endef
define Build/Compile
endef
define Package/aitvbox-mimi-update/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d $(1)/etc/smartdesktopmimi $(1)/usr/libexec
	$(INSTALL_BIN) $(PRODUCT_ROOT)/build/mimi-update/mimi-update $(1)/usr/bin/mimi-update
	$(INSTALL_BIN) ./files/mimi-update.init $(1)/etc/init.d/mimi-update
	$(INSTALL_BIN) ./files/mimi-desktop.init $(1)/etc/init.d/mimi-desktop
	$(INSTALL_BIN) ./files/mimi-platform-provider $(1)/usr/libexec/mimi-platform-provider
	if [ -d "$(PRODUCT_ROOT)/build/mimi-baseline" ]; then \
		python3 "$(PRODUCT_ROOT)/tools/update/baseline.py" verify --output "$(PRODUCT_ROOT)/build/mimi-baseline" && \
		$(CP) "$(PRODUCT_ROOT)/build/mimi-baseline/rootfs/." "$(1)/"; \
	fi
	# Enrollment, trust, signed factory tree and provider are platform release inputs.
	# Never ship a wildcard identity or development signing key to make this start.
endef
$(eval $(call BuildPackage,aitvbox-mimi-update))
