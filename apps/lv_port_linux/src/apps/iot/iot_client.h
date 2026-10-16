#ifndef MIMI_IOT_CLIENT_H
#define MIMI_IOT_CLIENT_H
#include <stddef.h>
/* Application-only HTTPS boundary; no project credential or hardware access. */
int mimi_iot_request(const char * path, const char * command, char * response, size_t capacity);
/* Read-only file presence: 0 absent, 1 present, -1 inaccessible. No credential read. */
int mimi_iot_configuration_present(void);
#endif
