include $(TOPDIR)/rules.mk

PKG_NAME:=aitvbox-suite
PKG_VERSION:=1.0.0
PKG_RELEASE:=12

include $(INCLUDE_DIR)/package.mk

# Out-of-tree 集成：本包不编译，只把产品仓库已构建好的产物注入 rootfs。
# AITVBOX_PRODUCT_BUILD 指向产品仓库根（含 build/、vendor/、apps/），由 build_firmware.sh
# 在调用 SDK 构建前 export。未设置时直接报错，避免拷空路径。
ifeq ($(origin AITVBOX_PRODUCT_BUILD),undefined)
$(error AITVBOX_PRODUCT_BUILD is not set. Run via scripts/build_firmware.sh which exports it.)
endif
PRODUCT_ROOT := $(abspath $(AITVBOX_PRODUCT_BUILD))
BUILD_OUT    := $(PRODUCT_ROOT)/build
VENDOR_LIBS  := $(PRODUCT_ROOT)/vendor/tuyaopen-a133-b6-libs
LV_PORT_DIR  := $(PRODUCT_ROOT)/apps/lv_port_linux
SWUPDATE_KEYS := $(PRODUCT_ROOT)/integrations/swupdate/keys
EMOTION_LOTTIE_PACK := $(PRODUCT_ROOT)/third_party/aitvbox_emotion_lottie_pack
AUDIO_DIAGNOSTIC_ENABLED := $(filter 1 y yes true,$(AITVBOX_AUDIO_DIAGNOSTIC))
CLOUD_100ASK_ENABLED := $(filter 1 y yes true on ON TRUE YES,$(AITVBOX_ENABLE_100ASK_CLOUD))
CLOUD_100ASK_DEPENDS :=
ifneq ($(CLOUD_100ASK_ENABLED),)
CLOUD_100ASK_DEPENDS += +libmosquitto-ssl
endif

PKG_FILE_DEPENDS += ./files/aitvbox.init
PKG_FILE_DEPENDS += ./files/start-ui.sh
PKG_FILE_DEPENDS += ./files/mount-sdcard.sh
PKG_FILE_DEPENDS += ./files/aitvbox-data.init
PKG_FILE_DEPENDS += ./files/aitvbox-bluetooth.init
PKG_FILE_DEPENDS += ./files/bt_init.sh
PKG_FILE_DEPENDS += ./files/bluetooth.json
PKG_FILE_DEPENDS += $(BUILD_OUT)/lv_port_linux/lv_backend
PKG_FILE_DEPENDS += $(BUILD_OUT)/lv_port_linux/lvglsim
PKG_FILE_DEPENDS += $(BUILD_OUT)/hdmi_preview/hdmi_preview
PKG_FILE_DEPENDS += $(BUILD_OUT)/tuyaopen/your_chat_bot_QIO_1.0.1.bin
PKG_FILE_DEPENDS += $(VENDOR_LIBS)/MNN/libMNN.so
PKG_FILE_DEPENDS += $(VENDOR_LIBS)/MNN/libMNN_Express.so
PKG_FILE_DEPENDS += $(VENDOR_LIBS)/models/mdtc_chunk_300ms.mnn
PKG_FILE_DEPENDS += $(VENDOR_LIBS)/models/tokens.txt
PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-version
PKG_FILE_DEPENDS += $(BUILD_OUT)/aitvbox-tuya-pid
PKG_FILE_DEPENDS += $(SWUPDATE_KEYS)/swupdate_public.pem
ifneq ($(AUDIO_DIAGNOSTIC_ENABLED),)
PKG_FILE_DEPENDS += $(PRODUCT_ROOT)/tools/board/audio_chain_boot_test.init
PKG_FILE_DEPENDS += $(PRODUCT_ROOT)/tools/board/audio-chain-test.wav
endif

define Package/aitvbox-suite
  SECTION:=utils
  CATEGORY:=Allwinner
  SUBMENU:=Custom
  TITLE:=A133 AI TVBox UI, Tuya AI runtime, and HDMI preview
  DEPENDS:=+aitvbox-platform +boot-play +alsa-lib +alsa-utils +libsamplerate +bluez-daemon +bluez-utils +bluez-alsa +btmanager-v4.0 +sbc \
    +libatomic \
    +libcedarx +libuapi +libevdev +libstdcpp +libjpeg-turbo +wifimanager-v2.0-lib $(CLOUD_100ASK_DEPENDS) +libcurl +ca-bundle
endef

define Package/aitvbox-suite/description
  Installs the A133 LVGL frontend/backend, Tuya AI chatbot runtime, MNN
  shared libraries, wakeup models, CJK fonts, HDMI preview helper, and
  an init script for product startup.
endef

define Build/Prepare
endef

define Build/Configure
endef

define Build/Compile
endef

define Install/audio-diagnostic
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(PRODUCT_ROOT)/tools/board/audio_chain_boot_test.init $(1)/etc/init.d/aitvbox-audio-diagnostic
	$(INSTALL_DATA) $(PRODUCT_ROOT)/tools/board/audio-chain-test.wav $(1)/etc/audio-chain-test.wav
endef

define Install/100ask-cloud-config
	$(INSTALL_DIR) $(1)/etc/100ask
	@if [ -f "$(PRODUCT_ROOT)/secrets/100ask_cloud.env" ]; then \
		cp -f "$(PRODUCT_ROOT)/secrets/100ask_cloud.env" $(1)/etc/100ask/cloud.conf; \
		echo "  install: /etc/100ask/cloud.conf (server config)"; \
	else \
		echo "  skip: secrets/100ask_cloud.env not found, using built-in defaults"; \
	fi
	@if [ -f "$(PRODUCT_ROOT)/secrets/100ask_ca.crt" ]; then \
		cp -f "$(PRODUCT_ROOT)/secrets/100ask_ca.crt" $(1)/etc/100ask/ca.crt; \
		echo "  install: /etc/100ask/ca.crt"; \
	fi
endef

define Package/aitvbox-suite/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(BUILD_OUT)/lv_port_linux/lv_backend $(1)/usr/bin/lv_backend
	$(INSTALL_BIN) $(BUILD_OUT)/lv_port_linux/lvglsim $(1)/usr/bin/lvglsim
	$(INSTALL_BIN) $(BUILD_OUT)/hdmi_preview/hdmi_preview $(1)/usr/bin/hdmi_preview
	$(INSTALL_BIN) $(BUILD_OUT)/tuyaopen/your_chat_bot_QIO_1.0.1.bin $(1)/usr/bin/your_chat_bot_QIO_1.0.1.bin
	$(INSTALL_BIN) ./files/start-ui.sh $(1)/usr/bin/aitvbox-start-ui
	$(INSTALL_BIN) ./files/mount-sdcard.sh $(1)/usr/bin/aitvbox-mount-sdcard

	$(INSTALL_DIR) $(1)/etc/init.d $(1)/etc/rc.d
	$(INSTALL_BIN) ./files/aitvbox-bluetooth.init $(1)/etc/init.d/aitvbox-bluetooth
	ln -sf ../init.d/aitvbox-bluetooth $(1)/etc/rc.d/S97aitvbox-bluetooth

	# Keep immutable recovery copies outside /etc/bluetooth.  That directory is
	# bind-mounted from rootfs_data and may contain zero-byte placeholders left
	# by an older image; aitvbox-data repairs only missing/empty static files and
	# preserves the adapter MAC and pairing database.
	$(INSTALL_DIR) $(1)/usr/share/aitvbox/bluetooth
	$(INSTALL_DATA) ./files/bluetooth.json $(1)/usr/share/aitvbox/bluetooth/bluetooth.json
	$(INSTALL_BIN) ./files/bt_init.sh $(1)/usr/share/aitvbox/bluetooth/bt_init.sh

	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_DATA) $(VENDOR_LIBS)/MNN/libMNN.so $(1)/usr/lib/libMNN.so
	$(INSTALL_DATA) $(VENDOR_LIBS)/MNN/libMNN_Express.so $(1)/usr/lib/libMNN_Express.so

	$(INSTALL_DIR) $(1)/usr/share/tuyaopen_models
	$(INSTALL_DATA) $(VENDOR_LIBS)/models/mdtc_chunk_300ms.mnn $(1)/usr/share/tuyaopen_models/mdtc_chunk_300ms.mnn
	$(INSTALL_DATA) $(VENDOR_LIBS)/models/tokens.txt $(1)/usr/share/tuyaopen_models/tokens.txt

	# 涂鸦量产授权目录；S95 启动脚本将其 bind 到 /overlay/factory/tuya。
	# 通用固件仅创建目录，不包含任何设备 UUID/AuthKey。
	$(INSTALL_DIR) $(1)/factory/tuya
	chmod 700 $(1)/factory/tuya

	$(INSTALL_DIR) $(1)/usr/share/fonts
	$(INSTALL_DATA) $(LV_PORT_DIR)/src/ui/font/SarasaUiSC-Regular.ttf $(1)/usr/share/fonts/SarasaUiSC-Regular.ttf
	$(INSTALL_DATA) $(LV_PORT_DIR)/src/ui/font/SarasaUiSC-SemiBold.ttf $(1)/usr/share/fonts/SarasaUiSC-SemiBold.ttf

	$(INSTALL_DIR) $(1)/usr/share/aitvbox/emotions
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Neutral & Thinking/static.json" $(1)/usr/share/aitvbox/emotions/static.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Neutral & Thinking/investigate.json" $(1)/usr/share/aitvbox/emotions/investigate.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Neutral & Thinking/ponder.json" $(1)/usr/share/aitvbox/emotions/ponder.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Neutral & Thinking/question.json" $(1)/usr/share/aitvbox/emotions/question.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Positive Emotions/smile.json" $(1)/usr/share/aitvbox/emotions/smile.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Negative Emotions/sad.json" $(1)/usr/share/aitvbox/emotions/sad.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Negative Emotions/angry.json" $(1)/usr/share/aitvbox/emotions/angry.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Surprise & Reactions/panic.json" $(1)/usr/share/aitvbox/emotions/panic.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Surprise & Reactions/shocked.json" $(1)/usr/share/aitvbox/emotions/shocked.json
	$(INSTALL_DATA) "$(EMOTION_LOTTIE_PACK)/Physical & Special States/asleep.json" $(1)/usr/share/aitvbox/emotions/asleep.json

	# OTA 验签公钥 + 固件版本号（OTA 版本比对与溯源）
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DATA) $(SWUPDATE_KEYS)/swupdate_public.pem $(1)/etc/swupdate_public.pem
	$(INSTALL_DATA) $(BUILD_OUT)/aitvbox-version $(1)/etc/aitvbox-version
	$(INSTALL_DATA) $(BUILD_OUT)/aitvbox-tuya-pid $(1)/etc/aitvbox-tuya-pid

	# Private cloud configuration is installed only for an explicitly enabled
	# internal provider build. Public builds never consume local cloud secrets.
	$(if $(CLOUD_100ASK_ENABLED),$(call Install/100ask-cloud-config,$(1)))

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/aitvbox.init $(1)/etc/init.d/aitvbox
	$(INSTALL_BIN) ./files/aitvbox-data.init $(1)/etc/init.d/aitvbox-data
	$(INSTALL_DIR) $(1)/etc/rc.d
	ln -sf ../init.d/aitvbox $(1)/etc/rc.d/S99aitvbox
	ln -sf ../init.d/aitvbox-data $(1)/etc/rc.d/S95aitvbox-data
	$(if $(AUDIO_DIAGNOSTIC_ENABLED),$(call Install/audio-diagnostic,$(1)))
endef

$(eval $(call BuildPackage,aitvbox-suite))
