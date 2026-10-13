#ifndef MIMI_OTA_SLOT_H
#define MIMI_OTA_SLOT_H
#include <stddef.h>
#include <string.h>

/* Parse one exact root= token; unrelated partitions= text and duplicate roots
 * must never authorize writing a slot. Used by the legacy OTA path too. */
static inline char ota_slot_from_cmdline(const char *line)
{
    char slot = 0;
    unsigned roots = 0;
    while (*line) {
        while (*line == ' ' || *line == '\t' || *line == '\n' || *line == '\r') ++line;
        const char *end = line;
        while (*end && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') ++end;
        size_t len = (size_t)(end - line);
        if (len >= 5 && memcmp(line, "root=", 5) == 0) {
            if (++roots > 1) return 0;
            if (len == sizeof("root=/dev/mmcblk0p4") - 1 &&
                memcmp(line, "root=/dev/mmcblk0p4", len) == 0) slot = 'A';
            else if (len == sizeof("root=/dev/mmcblk0p6") - 1 &&
                     memcmp(line, "root=/dev/mmcblk0p6", len) == 0) slot = 'B';
        }
        line = end;
    }
    return roots == 1 ? slot : 0;
}
#endif
