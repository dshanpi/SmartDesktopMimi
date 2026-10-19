#include "user_app_config.h"
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mimi_user_app_config_t g_cfg;
static bool initialized;

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

/* Platform-selected endpoints: empty/invalid means unavailable, never a
 * truncated path or an implicit fallback to another platform's service. */
static void set_app_path(char *out, size_t capacity, const char *value)
{
    out[0] = 0;
    size_t size = strlen(value);
    if(size < 2 || size >= capacity || value[0] != '/' || value[size - 1] == '/') return;
    const char *component = value + 1;
    for(const char *p = component;; p++) {
        unsigned char c = (unsigned char)*p;
        if(c && (c < 32 || c == 127)) return;
        if(c == '/' || !c) {
            size_t length = (size_t)(p - component);
            if(!length || (length == 1 && component[0] == '.') ||
               (length == 2 && component[0] == '.' && component[1] == '.')) return;
            if(!c) break;
            component = p + 1;
        }
    }
    memcpy(out, value, size + 1);
}

/* Consume complete physical lines. An embedded NUL or long line must not
 * turn into an apparently valid endpoint prefix or a second config record. */
static bool read_line(FILE *fp, char *line, size_t capacity, bool *invalid)
{
    size_t used = 0;
    int c;
    *invalid = false;
    while((c = fgetc(fp)) != EOF && c != '\n') {
        if(!c) *invalid = true;
        if(used + 1 < capacity) line[used++] = (char)c;
        else *invalid = true;
    }
    line[used] = 0;
    return used || c != EOF || *invalid;
}

static bool load_file(const char *path)
{
    FILE *fp;
    char line[512];

    fp = fopen(path, "r");
    if (!fp) return false;

    bool invalid_line, invalid_apps = false;
    while (read_line(fp, line, sizeof(line), &invalid_line)) {
        if(invalid_line) { invalid_apps = true; continue; }
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
        if(!strcmp(k, "user_apps.root")) set_app_path(g_cfg.user_apps_root, sizeof(g_cfg.user_apps_root), v);
        else if(!strcmp(k, "user_apps.socket_path")) set_app_path(g_cfg.user_apps_socket_path, sizeof(g_cfg.user_apps_socket_path), v);
    }

    if(invalid_apps || ferror(fp)) {
        g_cfg.user_apps_root[0] = 0;
        g_cfg.user_apps_socket_path[0] = 0;
    }
    fclose(fp);
    return true;
}

const mimi_user_app_config_t *mimi_user_app_config_get(void)
{
    if(initialized) return &g_cfg;
    snprintf(g_cfg.user_apps_root, sizeof(g_cfg.user_apps_root), "%s", "/overlay/aitvbox/apps");
    snprintf(g_cfg.user_apps_socket_path, sizeof(g_cfg.user_apps_socket_path), "%s", "/var/run/aitvbox/apps.sock");
    const char *path = getenv("LV_PORT_LINUX_CONFIG");
    if(path && *path) {
        if(!load_file(path)) {
            g_cfg.user_apps_root[0] = 0;
            g_cfg.user_apps_socket_path[0] = 0;
        }
    } else load_file("/etc/lv_port_linux.conf");
    initialized = true;
    return &g_cfg;
}
