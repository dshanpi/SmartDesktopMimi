#ifndef MIMI_VOICE_COMMAND_H
#define MIMI_VOICE_COMMAND_H
#include <stdbool.h>
typedef enum {
    MIMI_COMMAND_NONE,
    MIMI_COMMAND_HOME,
    MIMI_COMMAND_DRAWER,
    MIMI_COMMAND_SETTINGS,
    MIMI_COMMAND_WIFI,
    MIMI_COMMAND_BLUETOOTH,
    MIMI_COMMAND_VIDEO,
    MIMI_COMMAND_PAIRING,
    MIMI_COMMAND_IOT
} mimi_command_t;
/* Exact navigation vocabulary only. No shell, arbitrary tap, install or device control. */
bool mimi_command_parse(const char * text, mimi_command_t * command);
#endif
