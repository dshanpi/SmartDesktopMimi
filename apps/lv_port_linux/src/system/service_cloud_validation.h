#ifndef AITVBOX_SERVICE_CLOUD_VALIDATION_H
#define AITVBOX_SERVICE_CLOUD_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>

#define SERVICE_CLOUD_BIND_TOKEN_DIGITS 6U
#define SERVICE_CLOUD_CREDENTIAL_MIN_LENGTH 16U
#define SERVICE_CLOUD_CREDENTIAL_MAX_LENGTH 128U

bool service_cloud_credential_value_valid(const char *value, size_t min_len,
                                          size_t max_len);
bool service_cloud_bind_token_valid(const char *value);

#endif
