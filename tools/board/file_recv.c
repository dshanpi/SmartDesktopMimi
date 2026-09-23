/* Minimal one-shot TCP file receiver for serial-only board bring-up.
 * Usage: file_recv PORT OUTPUT
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s PORT OUTPUT\n", argv[0]);
        return 2;
    }

    char *end = NULL;
    long port = strtol(argv[1], &end, 10);
    if (!end || *end || port < 1 || port > 65535) {
        fprintf(stderr, "invalid port\n");
        return 2;
    }

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return 1;
    }
    int one = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(server, 1) < 0) {
        perror("bind/listen");
        close(server);
        return 1;
    }

    fprintf(stderr, "READY %ld\n", port);
    int client = accept(server, NULL, NULL);
    if (client < 0) {
        perror("accept");
        close(server);
        return 1;
    }

    int output = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (output < 0) {
        perror("open");
        close(client);
        close(server);
        return 1;
    }

    unsigned long long total = 0;
    char buf[32768];
    for (;;) {
        ssize_t got = read(client, buf, sizeof(buf));
        if (got == 0)
            break;
        if (got < 0) {
            if (errno == EINTR)
                continue;
            perror("read");
            return 1;
        }
        char *p = buf;
        ssize_t left = got;
        while (left > 0) {
            ssize_t put = write(output, p, (size_t)left);
            if (put < 0) {
                if (errno == EINTR)
                    continue;
                perror("write");
                return 1;
            }
            p += put;
            left -= put;
        }
        total += (unsigned long long)got;
    }

    fsync(output);
    close(output);
    close(client);
    close(server);
    fprintf(stderr, "DONE %llu\n", total);
    return 0;
}
