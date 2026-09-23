#include "service_cloud_validation.h"

#include <string.h>

bool service_cloud_credential_value_valid(const char *value, size_t min_len,
                                          size_t max_len)
{
    if (!value || min_len > max_len) return false;

    size_t len = strlen(value);
    if (len < min_len || len > max_len) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') || ch == '-' || ch == '_' ||
              ch == '.' || ch == '~')) {
            return false;
        }
    }
    return true;
}

bool service_cloud_bind_token_valid(const char *value)
{
    if (!value || strlen(value) != SERVICE_CLOUD_BIND_TOKEN_DIGITS) {
        return false;
    }
    for (size_t i = 0; i < SERVICE_CLOUD_BIND_TOKEN_DIGITS; i++) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    return true;
}
