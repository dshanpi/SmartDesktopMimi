#ifndef MIMI_USER_APP_CONFIG_H
#define MIMI_USER_APP_CONFIG_H

/* Application-side platform endpoint selection, not an installation or
 * execution authorization. The platform service still owns those checks. */
typedef struct {
    char user_apps_root[256];
    char user_apps_socket_path[108];
} mimi_user_app_config_t;

const mimi_user_app_config_t *mimi_user_app_config_get(void);
#endif
