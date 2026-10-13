#ifndef MIMI_IOT_JSON_H
#define MIMI_IOT_JSON_H
#include <json-c/json.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
/* Parse one complete object, never a valid prefix of a malformed response. */
static inline json_object *mimi_iot_json_object(const char *data, size_t length)
{
    if (!data || !length || length > INT_MAX || memchr(data, 0, length)) return NULL;
    json_tokener *tok = json_tokener_new();
    if (!tok) return NULL;
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT);
    json_object *obj = json_tokener_parse_ex(tok, data, (int)length);
#if JSON_C_VERSION_NUM >= 0x000f00
    size_t end = json_tokener_get_parse_end(tok);
#else
    /* json-c 0.13 on supported host test machines exposes the offset here. */
    size_t end = (size_t)tok->char_offset;
#endif
    int valid = json_tokener_get_error(tok) == json_tokener_success && obj &&
                json_object_get_type(obj) == json_type_object;
    for (; end < length; end++) if (!isspace((unsigned char)data[end])) valid = 0;
    json_tokener_free(tok);
    if (!valid) { if (obj) json_object_put(obj); return NULL; }
    return obj;
}
static inline int mimi_iot_command_accepted(json_object *obj)
{
    json_object *accepted = NULL, *confirmed = NULL;
    return obj && json_object_get_type(obj) == json_type_object &&
           json_object_object_length(obj) == 2 &&
           json_object_object_get_ex(obj, "accepted", &accepted) &&
           json_object_get_type(accepted) == json_type_boolean && json_object_get_boolean(accepted) &&
           json_object_object_get_ex(obj, "state_confirmed", &confirmed) &&
           json_object_get_type(confirmed) == json_type_boolean && !json_object_get_boolean(confirmed);
}
#endif
