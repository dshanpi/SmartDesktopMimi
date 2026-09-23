#ifndef APP_CONFIG_DEFAULTS_H
#define APP_CONFIG_DEFAULTS_H

#define APP_CFG_DEFAULT_WIFI_IFNAME "wlan0"
#define APP_CFG_DEFAULT_WPA_CTRL_PATH "/etc/wifi/wpa_supplicant/sockets/wlan0"
/* TF card mount point (see packaging/aitvbox-suite/files/mount-sdcard.sh). */
#define APP_CFG_DEFAULT_VIDEO_DIR "/mnt/SDCARD"
#define APP_CFG_DEFAULT_IPC_SOCKET_PATH "/tmp/lv_port_linux_backend.sock"
#define APP_CFG_DEFAULT_IPC_SOCKET_MODE 0660

#define APP_CFG_DEFAULT_WIN_W 1024
#define APP_CFG_DEFAULT_WIN_H 768

#endif /* APP_CONFIG_DEFAULTS_H */
