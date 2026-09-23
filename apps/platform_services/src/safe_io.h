#ifndef AITVBOX_SAFE_IO_H
#define AITVBOX_SAFE_IO_H

#include <stddef.h>
#include <sys/types.h>

int aitvbox_atomic_write_file(const char *path, const void *data,
                              size_t length, mode_t mode);
int aitvbox_durable_unlink(const char *path);
void aitvbox_secure_clear(void *buffer, size_t length);

#endif
