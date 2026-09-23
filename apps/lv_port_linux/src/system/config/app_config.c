#include "app_config.h"
#include "app_config_defaults.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_CONFIG_PATH_ENV "LV_PORT_LINUX_CONFIG"
#define APP_CONFIG_DEFAULT_FILE "/etc/lv_port_linux.conf"

static app_config_t g_cfg;
static bool g_cfg_initialized = false;

static char *trim(char *s)
{
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static void set_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    snprintf(g_cfg.wifi_ifname, sizeof(g_cfg.wifi_ifname), "%s", APP_CFG_DEFAULT_WIFI_IFNAME);
    snprintf(g_cfg.wpa_ctrl_path, sizeof(g_cfg.wpa_ctrl_path), "%s", APP_CFG_DEFAULT_WPA_CTRL_PATH);
    snprintf(g_cfg.video_dir, sizeof(g_cfg.video_dir), "%s", APP_CFG_DEFAULT_VIDEO_DIR);
    snprintf(g_cfg.ipc_socket_path, sizeof(g_cfg.ipc_socket_path), "%s", APP_CFG_DEFAULT_IPC_SOCKET_PATH);
    g_cfg.ipc_socket_mode = APP_CFG_DEFAULT_IPC_SOCKET_MODE;
    g_cfg.sim_window_width = APP_CFG_DEFAULT_WIN_W;
    g_cfg.sim_window_height = APP_CFG_DEFAULT_WIN_H;
}

static void apply_kv(const char *key, const char *value)
{
    if (strcmp(key, "wifi.ifname") == 0) {
        snprintf(g_cfg.wifi_ifname, sizeof(g_cfg.wifi_ifname), "%s", value);
    } else if (strcmp(key, "wifi.wpa_ctrl_path") == 0) {
        snprintf(g_cfg.wpa_ctrl_path, sizeof(g_cfg.wpa_ctrl_path), "%s", value);
    } else if (strcmp(key, "video.dir") == 0) {
        snprintf(g_cfg.video_dir, sizeof(g_cfg.video_dir), "%s", value);
    } else if (strcmp(key, "ipc.socket_path") == 0) {
        snprintf(g_cfg.ipc_socket_path, sizeof(g_cfg.ipc_socket_path), "%s", value);
    } else if (strcmp(key, "ipc.socket_mode") == 0) {
        g_cfg.ipc_socket_mode = (int)strtol(value, NULL, 8);
    } else if (strcmp(key, "ui.window_width") == 0) {
        g_cfg.sim_window_width = atoi(value);
    } else if (strcmp(key, "ui.window_height") == 0) {
        g_cfg.sim_window_height = atoi(value);
    }
}

static void load_file(const char *path)
{
    FILE *fp;
    char line[512];

    fp = fopen(path, "r");
    if (!fp) return;

    while (fgets(line, sizeof(line), fp)) {
        char *eq;
        char *k;
        char *v;
        k = trim(line);
        if (*k == '\0' || *k == '#') continue;

        eq = strchr(k, '=');
        if (!eq) continue;
        *eq = '\0';
        v = trim(eq + 1);
        k = trim(k);
        apply_kv(k, v);
    }

    fclose(fp);
}

int app_config_init(void)
{
    const char *path;
    if (g_cfg_initialized) return 0;

    set_defaults();
    path = getenv(APP_CONFIG_PATH_ENV);
    if (path && *path) {
        load_file(path);
    } else {
        load_file(APP_CONFIG_DEFAULT_FILE);
    }
    g_cfg_initialized = true;
    return 0;
}

const app_config_t *app_config_get(void)
{
    if (!g_cfg_initialized) app_config_init();
    return &g_cfg;
}
