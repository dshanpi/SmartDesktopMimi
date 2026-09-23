#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define APP_SOCKET "/var/run/aitvbox/apps.sock"

static int write_all(int fd, const char *data, size_t length)
{
    while (length) {
        ssize_t count = write(fd, data, length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        data += count;
        length -= (size_t)count;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s APP_ID ACTION\n", argv[0]);
        return 2;
    }
    if (strpbrk(argv[1], "\"\\\n\r") || strpbrk(argv[2], "\"\\\n\r")) {
        fprintf(stderr, "invalid argument\n");
        return 2;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", APP_SOCKET);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        perror("connect");
        close(fd);
        return 1;
    }

    char request[512];
    int length = snprintf(
        request, sizeof(request),
        "{\"command\":\"invoke\",\"appId\":\"%s\",\"action\":\"%s\"}\n",
        argv[1], argv[2]);
    if (length < 0 || (size_t)length >= sizeof(request) ||
        write_all(fd, request, (size_t)length)) {
        fprintf(stderr, "cannot send request\n");
        close(fd);
        return 1;
    }

    char response[8192];
    size_t used = 0;
    while (used + 1 < sizeof(response)) {
        ssize_t count = read(fd, response + used, sizeof(response) - used - 1);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            perror("read");
            close(fd);
            return 1;
        }
        if (count == 0)
            break;
        used += (size_t)count;
        if (memchr(response, '\n', used))
            break;
    }
    response[used] = '\0';
    fputs(response, stdout);
    close(fd);
    return strstr(response, "\"ok\":true") ? 0 : 1;
}
