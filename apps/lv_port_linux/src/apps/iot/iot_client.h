#ifndef MIMI_IOT_CLIENT_H
#define MIMI_IOT_CLIENT_H
#include <stddef.h>
/* Application-only HTTPS boundary; no project credential or hardware access. */
int mimi_iot_request(const char * path, const char * command, char * response, size_t capacity);
#endif
