#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>

typedef struct {
    char wifi_ifname[16];
    char wpa_ctrl_path[128];
    char video_dir[256];
    char ipc_socket_path[108];
    int ipc_socket_mode;
    int sim_window_width;
    int sim_window_height;
} app_config_t;

int app_config_init(void);
const app_config_t *app_config_get(void);

#endif /* APP_CONFIG_H */
