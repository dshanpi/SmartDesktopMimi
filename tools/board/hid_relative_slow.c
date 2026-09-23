#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int integer(const char *text, int minimum, int maximum, const char *name)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < minimum || value > maximum) {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        exit(2);
    }
    return (int)value;
}

static int write_report(int fd, signed char dx, signed char dy,
                        unsigned char buttons)
{
    unsigned char report[4] = {
        buttons, (unsigned char)dx, (unsigned char)dy, 0
    };
    ssize_t written;
    do {
        written = write(fd, report, sizeof(report));
    } while (written < 0 && errno == EINTR);
    return written == (ssize_t)sizeof(report) ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 6 && argc != 7) {
        fprintf(stderr,
                "usage: %s DEVICE DX DY STEPS INTERVAL_US [click]\n",
                argv[0]);
        return 2;
    }
    int dx = integer(argv[2], -127, 127, "DX");
    int dy = integer(argv[3], -127, 127, "DY");
    int steps = integer(argv[4], 0, 10000, "STEPS");
    int interval = integer(argv[5], 1000, 1000000, "INTERVAL_US");
    int click = argc == 7 ? integer(argv[6], 0, 1, "click") : 0;
    int fd = open(argv[1], O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    for (int i = 0; i < steps; i++) {
        if (write_report(fd, (signed char)dx, (signed char)dy, 0)) {
            perror("write HID movement");
            close(fd);
            return 1;
        }
        usleep((useconds_t)interval);
    }
    if (click) {
        if (write_report(fd, 0, 0, 1) ||
            (usleep(50000), write_report(fd, 0, 0, 0))) {
            perror("write HID click");
            close(fd);
            return 1;
        }
    }
    close(fd);
    return 0;
}
