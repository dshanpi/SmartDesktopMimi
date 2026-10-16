#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "desktop_widget_layout.h"
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static const char * names[] = {"empty", "assistant", "hdmi", "environment", "iot", "connection", "network", "system"};
static int compatible(unsigned slot, mimi_widget_kind kind)
{
    return slot < MIMI_WIDGET_SLOTS && kind >= 0 && kind < MIMI_WIDGET_COUNT &&
           (kind == MIMI_WIDGET_EMPTY || ((slot < 2) == (kind <= MIMI_WIDGET_HDMI)));
}
void mimi_widget_layout_default(mimi_widget_layout * l)
{
    *l = (mimi_widget_layout){.slots = {MIMI_WIDGET_ASSISTANT, MIMI_WIDGET_HDMI, MIMI_WIDGET_ENVIRONMENT,
                                        MIMI_WIDGET_IOT, MIMI_WIDGET_CONNECTION}};
}
int mimi_widget_layout_valid(const mimi_widget_layout * l)
{
    if(!l) return 0;
    unsigned seen = 0;
    for(unsigned i = 0; i < MIMI_WIDGET_SLOTS; i++) {
        mimi_widget_kind k = l->slots[i];
        if(!compatible(i, k) || (k && (seen & (1u << k)))) return 0;
        if(k) seen |= 1u << k;
    }
    return 1;
}
int mimi_widget_layout_set(mimi_widget_layout * l, unsigned slot, mimi_widget_kind kind)
{
    if(!l || !compatible(slot, kind)) return MIMI_LAYOUT_INVALID;
    mimi_widget_layout next = *l;
    next.slots[slot]        = kind;
    if(!mimi_widget_layout_valid(&next)) return MIMI_LAYOUT_INVALID;
    *l = next;
    return 0;
}
int mimi_widget_layout_swap(mimi_widget_layout * l, unsigned a, unsigned b)
{
    if(!l || a >= MIMI_WIDGET_SLOTS || b >= MIMI_WIDGET_SLOTS) return MIMI_LAYOUT_INVALID;
    mimi_widget_layout next = *l;
    mimi_widget_kind k      = next.slots[a];
    next.slots[a]           = next.slots[b];
    next.slots[b]           = k;
    if(!mimi_widget_layout_valid(&next)) return MIMI_LAYOUT_INVALID;
    *l = next;
    return 0;
}
int mimi_widget_layout_path(char * path, unsigned capacity)
{
    const char * base = getenv("LV_SETTINGS_FILE");
    int n;
    if(base && *base)
        n = snprintf(path, capacity, "%s.widgets.json", base);
    else {
        base = getenv("HOME");
        n    = snprintf(path, capacity, "%s/.lv_port_linux_settings.bin.widgets.json", base && *base ? base : "/tmp");
    }
    return n < 0 || (unsigned)n >= capacity ? MIMI_LAYOUT_IO : 0;
}
int mimi_widget_layout_load(const char * path, mimi_widget_layout * out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if(fd < 0) {
        if(errno == ENOENT) {
            mimi_widget_layout_default(out);
            return 0;
        }
        return MIMI_LAYOUT_IO;
    }
    struct stat st;
    char data[2049];
    size_t n = 0;
    if(fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 1 || st.st_size > 2048) {
        close(fd);
        return MIMI_LAYOUT_INVALID;
    }
    while(n < sizeof(data) - 1) {
        ssize_t got = read(fd, data + n, sizeof(data) - 1 - n);
        if(got < 0 && errno == EINTR) continue;
        if(got < 0) {
            close(fd);
            return MIMI_LAYOUT_IO;
        }
        if(!got) break;
        n += (size_t)got;
    }
    close(fd);
    data[n]                   = 0;
    struct json_tokener * tok = json_tokener_new();
    if(!tok) return MIMI_LAYOUT_IO;
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT);
    struct json_object *root = json_tokener_parse_ex(tok, data, (int)n), *schema = NULL, *rev = NULL, *slots = NULL;
    int rc                  = MIMI_LAYOUT_INVALID;
    mimi_widget_layout next = {0};
    if(json_tokener_get_error(tok) != json_tokener_success || !root || !json_object_is_type(root, json_type_object))
        goto done;
    /* Strict field count rejects new schema fields rather than discarding data. */
    if(json_object_object_length(root) != 3 || !json_object_object_get_ex(root, "schema", &schema) ||
       !json_object_is_type(schema, json_type_int) || json_object_get_int64(schema) != 1 ||
       !json_object_object_get_ex(root, "revision", &rev) || !json_object_is_type(rev, json_type_int) ||
       json_object_get_int64(rev) < 0 || json_object_get_int64(rev) > UINT32_MAX ||
       !json_object_object_get_ex(root, "slots", &slots) || !json_object_is_type(slots, json_type_array) ||
       json_object_array_length(slots) != MIMI_WIDGET_SLOTS)
        goto done;
    next.revision = (uint32_t)json_object_get_int64(rev);
    for(unsigned i = 0; i < MIMI_WIDGET_SLOTS; i++) {
        struct json_object * v = json_object_array_get_idx(slots, i);
        if(!json_object_is_type(v, json_type_string)) goto done;
        const char * s = json_object_get_string(v);
        unsigned k;
        for(k = 0; k < MIMI_WIDGET_COUNT; k++)
            if(strlen(names[k]) == (size_t)json_object_get_string_len(v) && !strcmp(s, names[k])) break;
        if(k == MIMI_WIDGET_COUNT) goto done;
        next.slots[i] = (mimi_widget_kind)k;
    }
    if(mimi_widget_layout_valid(&next)) {
        *out = next;
        rc   = 0;
    }
done:
    if(root) json_object_put(root);
    json_tokener_free(tok);
    return rc;
}
int mimi_widget_layout_save(const char * path, mimi_widget_layout * layout)
{
    if(!mimi_widget_layout_valid(layout) || layout->revision == UINT32_MAX) return MIMI_LAYOUT_INVALID;
    char temp[PATH_MAX], lockpath[PATH_MAX], dir[PATH_MAX], data[512];
    if(snprintf(temp, sizeof(temp), "%s.tmp.XXXXXX", path) >= (int)sizeof(temp) ||
       snprintf(lockpath, sizeof(lockpath), "%s.lock", path) >= (int)sizeof(lockpath) || strlen(path) >= sizeof(dir))
        return MIMI_LAYOUT_IO;
    strcpy(dir, path);
    char * slash = strrchr(dir, '/');
    if(slash) {
        if(slash == dir)
            slash[1] = 0;
        else
            *slash = 0;
    } else
        strcpy(dir, ".");
    int rc = MIMI_LAYOUT_IO, fd = -1, dfd = -1,
        lockfd = open(lockpath, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if(lockfd < 0) return rc;
    if(flock(lockfd, LOCK_EX)) goto done;
    mimi_widget_layout previous;
    rc = mimi_widget_layout_load(path, &previous);
    if(rc) goto done;
    if(previous.revision != layout->revision) {
        rc = MIMI_LAYOUT_CONFLICT;
        goto done;
    }
    rc  = MIMI_LAYOUT_IO;
    dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(dfd < 0) goto done;
    fd = mkstemp(temp);
    if(fd < 0) goto done;
    int n =
        snprintf(data, sizeof(data), "{\"schema\":1,\"revision\":%u,\"slots\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
                 layout->revision + 1, names[layout->slots[0]], names[layout->slots[1]], names[layout->slots[2]],
                 names[layout->slots[3]], names[layout->slots[4]]);
    size_t offset = 0;
    while(offset < (size_t)n) {
        ssize_t wrote = write(fd, data + offset, n - offset);
        if(wrote < 0 && errno == EINTR) continue;
        if(wrote <= 0) goto done;
        offset += (size_t)wrote;
    }
    if(fsync(fd)) goto done;
    if(close(fd)) {
        fd = -1;
        goto done;
    }
    fd = -1;
    if(rename(temp, path)) goto done;
    temp[0] = 0;
    layout->revision++;
    /* Rename committed: report uncertain durability without claiming old data remains. */
    rc = fsync(dfd) ? MIMI_LAYOUT_DURABILITY_WARNING : MIMI_LAYOUT_OK;
done:
    if(fd >= 0) close(fd);
    if(temp[0] && strstr(temp, "XXXXXX") == NULL) unlink(temp);
    if(dfd >= 0) close(dfd);
    close(lockfd);
    return rc;
}
