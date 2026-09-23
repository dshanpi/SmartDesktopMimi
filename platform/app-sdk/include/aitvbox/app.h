#ifndef AITVBOX_APP_H
#define AITVBOX_APP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AITVBOX_APP_API_VERSION 1

const char *aitvbox_app_action(int argc, char **argv);
int aitvbox_app_reply(bool ok, const char *message);

int aitvbox_capability_call(const char *capability, const char *arguments_json,
                            char *result_json, size_t result_size);
int aitvbox_keyboard(unsigned keycode, unsigned modifier);
int aitvbox_pointer(int dx, int dy, unsigned buttons, int wheel);
int aitvbox_storage_read(const char *key, char *value, size_t value_size,
                         bool *found);
int aitvbox_storage_write(const char *key, const char *value);

#ifdef __cplusplus
}
#endif

#endif
