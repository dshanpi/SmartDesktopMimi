#ifndef AI_FREE_CHAT_VIEW_H
#define AI_FREE_CHAT_VIEW_H

#include "../../system/backend_types.h"
#include "lvgl.h"

#include <stdbool.h>

typedef void (*ai_free_chat_exit_cb_t)(void *user_data);

void ai_free_chat_view_open(ai_free_chat_exit_cb_t exit_cb, void *user_data);
void ai_free_chat_view_close(void);
bool ai_free_chat_view_is_open(void);
void ai_free_chat_view_set_status(const ai_status_t *status);
void ai_free_chat_view_set_text(const char *text);

#endif /* AI_FREE_CHAT_VIEW_H */
