#ifndef MIMI_DESKTOP_WIDGET_LAYOUT_H
#define MIMI_DESKTOP_WIDGET_LAYOUT_H
#include <stdint.h>
#define MIMI_WIDGET_SLOTS 5
typedef enum {
    MIMI_WIDGET_EMPTY,
    MIMI_WIDGET_ASSISTANT,
    MIMI_WIDGET_HDMI,
    MIMI_WIDGET_ENVIRONMENT,
    MIMI_WIDGET_IOT,
    MIMI_WIDGET_CONNECTION,
    MIMI_WIDGET_NETWORK,
    MIMI_WIDGET_SYSTEM,
    MIMI_WIDGET_COUNT
} mimi_widget_kind;
typedef struct
{
    uint32_t revision;
    mimi_widget_kind slots[MIMI_WIDGET_SLOTS];
} mimi_widget_layout;
/* Errors never alter the caller's layout or replace an existing file. */
enum {
    MIMI_LAYOUT_OK                 = 0,
    MIMI_LAYOUT_DURABILITY_WARNING = 1,
    MIMI_LAYOUT_INVALID            = -1,
    MIMI_LAYOUT_IO                 = -2,
    MIMI_LAYOUT_CONFLICT           = -3
};
void mimi_widget_layout_default(mimi_widget_layout * layout);
int mimi_widget_layout_valid(const mimi_widget_layout * layout);
int mimi_widget_layout_set(mimi_widget_layout * layout, unsigned slot, mimi_widget_kind kind);
int mimi_widget_layout_swap(mimi_widget_layout * layout, unsigned first, unsigned second);
int mimi_widget_layout_load(const char * path, mimi_widget_layout * layout);
int mimi_widget_layout_save(const char * path, mimi_widget_layout * layout);
int mimi_widget_layout_path(char * path, unsigned capacity);
#endif
