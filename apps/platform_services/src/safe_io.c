#define _GNU_SOURCE

#include "safe_io.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TEMPORARY_FILE_ATTEMPTS 128U

static int write_all(int fd, const void *data, size_t length)
{
    const unsigned char *cursor = data;
    while (length > 0) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            if (written == 0)
                errno = EIO;
            return -1;
        }
        cursor += written;
        length -= (size_t)written;
    }
    return 0;
}

static int open_parent_directory(const char *path, char *basename,
                                 size_t basename_size)
{
    char copy[PATH_MAX];
    char *slash;
    const char *directory;
    const char *name;
    size_t length;

    if (!path || !path[0] || !basename || basename_size == 0) {
        errno = EINVAL;
        return -1;
    }
    length = strlen(path);
    if (length >= sizeof(copy)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(copy, path, length + 1);
    slash = strrchr(copy, '/');
    if (!slash) {
        directory = ".";
        name = copy;
    } else if (slash == copy) {
        directory = "/";
        name = slash + 1;
    } else {
        *slash = '\0';
        directory = copy;
        name = slash + 1;
    }
    if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..")) {
        errno = EINVAL;
        return -1;
    }
    if (snprintf(basename, basename_size, "%s", name) >=
        (int)basename_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}

int aitvbox_atomic_write_file(const char *path, const void *data,
                              size_t length, mode_t mode)
{
    char basename[NAME_MAX + 1];
    char temporary[96];
    int directory_fd = -1;
    int file_fd = -1;
    int saved_errno = 0;
    bool temporary_created = false;

    if ((!data && length > 0) || (mode & ~0777U) != 0) {
        errno = EINVAL;
        return -1;
    }
    directory_fd = open_parent_directory(path, basename, sizeof(basename));
    if (directory_fd < 0)
        return -1;

    for (unsigned int attempt = 0;
         attempt < TEMPORARY_FILE_ATTEMPTS; attempt++) {
        int written = snprintf(temporary, sizeof(temporary),
                               ".aitvbox-tmp.%ld.%u",
                               (long)getpid(), attempt);
        if (written < 0 || written >= (int)sizeof(temporary)) {
            errno = ENAMETOOLONG;
            goto fail;
        }
        file_fd = openat(directory_fd, temporary,
                         O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                         mode);
        if (file_fd >= 0) {
            temporary_created = true;
            break;
        }
        if (errno != EEXIST)
            goto fail;
    }
    if (file_fd < 0) {
        errno = EEXIST;
        goto fail;
    }
    if (fchmod(file_fd, mode) || write_all(file_fd, data, length) ||
        fsync(file_fd)) {
        goto fail;
    }
    if (close(file_fd)) {
        file_fd = -1;
        goto fail;
    }
    file_fd = -1;
    if (renameat(directory_fd, temporary, directory_fd, basename))
        goto fail;
    temporary_created = false;
    if (fsync(directory_fd))
        goto fail;
    if (close(directory_fd))
        return -1;
    return 0;

fail:
    saved_errno = errno;
    if (file_fd >= 0)
        close(file_fd);
    if (temporary_created)
        unlinkat(directory_fd, temporary, 0);
    if (directory_fd >= 0)
        close(directory_fd);
    errno = saved_errno;
    return -1;
}

int aitvbox_durable_unlink(const char *path)
{
    char basename[NAME_MAX + 1];
    int directory_fd = open_parent_directory(path, basename, sizeof(basename));
    if (directory_fd < 0)
        return -1;
    if (unlinkat(directory_fd, basename, 0)) {
        int saved_errno = errno;
        close(directory_fd);
        if (saved_errno == ENOENT)
            return 0;
        errno = saved_errno;
        return -1;
    }
    if (fsync(directory_fd)) {
        int saved_errno = errno;
        close(directory_fd);
        errno = saved_errno;
        return -1;
    }
    return close(directory_fd);
}

void aitvbox_secure_clear(void *buffer, size_t length)
{
    volatile unsigned char *cursor = buffer;
    while (length-- > 0)
        *cursor++ = 0;
}
