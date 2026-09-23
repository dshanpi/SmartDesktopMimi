#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct sockaddr_in dst = {0};
    int fd;
    ssize_t sent;

    if (argc != 4) {
        fprintf(stderr, "usage: %s ADDRESS PORT PAYLOAD\n", argv[0]);
        return 2;
    }

    dst.sin_family = AF_INET;
    dst.sin_port = htons((unsigned short)strtoul(argv[2], NULL, 10));
    if (inet_pton(AF_INET, argv[1], &dst.sin_addr) != 1) {
        fprintf(stderr, "invalid address: %s\n", argv[1]);
        return 2;
    }

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    sent = sendto(fd, argv[3], strlen(argv[3]), 0,
                  (const struct sockaddr *)&dst, sizeof(dst));
    if (sent < 0) {
        perror("sendto");
        close(fd);
        return 1;
    }

    close(fd);
    return sent == (ssize_t)strlen(argv[3]) ? 0 : 1;
}
