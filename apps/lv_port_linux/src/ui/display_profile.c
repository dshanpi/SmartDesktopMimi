#include "display_profile.h"
#include <stdio.h>
#include <string.h>
#ifndef MIMI_DISPLAY_PROFILES
#define MIMI_DISPLAY_PROFILES "1024x768"
#endif
const char * mimi_display_profile_list(void)
{
    return MIMI_DISPLAY_PROFILES;
}
bool mimi_display_profile_allows(int width, int height)
{
    if(width <= 0 || height <= 0) return false;
    char wanted[32];
    snprintf(wanted, sizeof(wanted), "%dx%d", width, height);
    size_t n       = strlen(wanted);
    const char * p = MIMI_DISPLAY_PROFILES;
    while(*p) {
        const char * end = strchr(p, ',');
        size_t length    = end ? (size_t)(end - p) : strlen(p);
        if(length == n && !memcmp(p, wanted, n)) return true;
        if(!end) break;
        p = end + 1;
    }
    return false;
}
