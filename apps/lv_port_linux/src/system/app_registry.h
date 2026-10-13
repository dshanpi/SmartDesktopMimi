#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "../apps/app.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../ui/ui_i18n.h"

typedef enum {
    APP_ICON_BUILTIN = 0,
    APP_ICON_FILE,
    APP_ICON_GENERATED,
} app_icon_type_t;

typedef struct {
    app_id_t id;
    const char *name;
    ui_string_id_t name_id;
    app_icon_type_t icon_type;
    const void *icon_src;       /* Image descriptor/path, or NULL for a generated icon. */
    uint32_t accent_color;
    bool show_in_launcher;
    AppDescriptor *descriptor;
} app_registry_entry_t;

const app_registry_entry_t * app_registry_get_entry(app_id_t id);
AppDescriptor * app_registry_get_descriptor(app_id_t id);
size_t app_registry_get_launcher_count(void);
const app_registry_entry_t * app_registry_get_launcher_entry(size_t index);

#endif /* APP_REGISTRY_H */
