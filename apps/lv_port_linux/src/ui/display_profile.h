#ifndef MIMI_DISPLAY_PROFILE_H
#define MIMI_DISPLAY_PROFILE_H
#include <stdbool.h>
/* Read-only build selection, never a desktop settings page. */
bool mimi_display_profile_allows(int width, int height);
const char * mimi_display_profile_list(void);
#endif
