#ifndef MIMI_AI_COMMAND_REPLY_H
#define MIMI_AI_COMMAND_REPLY_H
#include "ai_command_transaction.h"
#include <json-c/json.h>
#include <json-c/json_tokener.h>

typedef struct {
    mimi_ai_command command;
    char id[65];
    int result;
} mimi_ai_reply;

/* Count top-level members independently: json-c replaces duplicate keys.
 * Syntax validation remains the JSON parser's responsibility. */
static inline unsigned mimi_ai_reply_members(const char *data, size_t len)
{
    unsigned count = 0;
    int depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < len; ++i) {
        char c = data[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') --depth;
        else if (c == ':' && depth == 1) ++count;
    }
    return count;
}

static inline bool mimi_ai_reply_string(struct json_object *obj, const char *key,
                                        const char **value)
{
    struct json_object *field;
    if (!json_object_object_get_ex(obj, key, &field) ||
        !json_object_is_type(field, json_type_string)) return false;
    *value = json_object_get_string(field);
    return (size_t)json_object_get_string_len(field) == strlen(*value);
}

static inline bool mimi_ai_reply_parse(const char *data, size_t len, mimi_ai_reply *out)
{
    struct json_tokener *tok;
    struct json_object *obj, *result;
    const char *runtime, *command, *id;
    mimi_ai_reply parsed = {0};
    bool ok = false;
    if (!data || !out || len == 0 || len > 512 || memchr(data, 0, len)) return false;
    tok = json_tokener_new();
    if (!tok) return false;
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT);
    obj = json_tokener_parse_ex(tok, data, (int)len);
    if (json_tokener_get_error(tok) != json_tokener_success ||
        (size_t)tok->char_offset != len || !json_object_is_type(obj, json_type_object) ||
        json_object_object_length(obj) != 4 || mimi_ai_reply_members(data, len) != 4)
        goto done;
    if (!mimi_ai_reply_string(obj, "runtime", &runtime) || strcmp(runtime, "tuya") ||
        !mimi_ai_reply_string(obj, "command_result", &command) ||
        !mimi_ai_reply_string(obj, "request_id", &id) || !mimi_ai_id_valid(id) ||
        !json_object_object_get_ex(obj, "result", &result) ||
        !json_object_is_type(result, json_type_int)) goto done;
    if (!strcmp(command, "enter_free_chat")) parsed.command = MIMI_AI_ENTER;
    else if (!strcmp(command, "exit_free_chat")) parsed.command = MIMI_AI_EXIT;
    else goto done;
    int64_t code = json_object_get_int64(result);
    if (code < INT_MIN || code > INT_MAX) goto done;
    memcpy(parsed.id, id, strlen(id) + 1);
    parsed.result = (int)code;
    *out = parsed;
    ok = true;
done:
    if (obj) json_object_put(obj);
    json_tokener_free(tok);
    return ok;
}
#endif
