#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int emit(int fd, unsigned short type, unsigned short code, int value)
{
    struct input_event event = {
        .type = type,
        .code = code,
        .value = value,
    };
    return write(fd, &event, sizeof(event)) == (ssize_t)sizeof(event) ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: %s EVENT SCREEN_W SCREEN_H X Y\n", argv[0]);
        return 2;
    }
    int screen_width = atoi(argv[2]);
    int screen_height = atoi(argv[3]);
    int x = atoi(argv[4]);
    int y = atoi(argv[5]);
    if (screen_width < 1 || screen_height < 1 ||
        x < 0 || x >= screen_width || y < 0 || y >= screen_height)
        return 2;

    int fd = open(argv[1], O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    struct input_absinfo abs_x, abs_y;
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) ||
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y)) {
        perror("EVIOCGABS");
        close(fd);
        return 1;
    }
    int raw_x = abs_x.minimum +
        x * (abs_x.maximum - abs_x.minimum) / (screen_width - 1);
    int raw_y = abs_y.minimum +
        y * (abs_y.maximum - abs_y.minimum) / (screen_height - 1);

    int failed =
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 1) ||
        emit(fd, EV_ABS, ABS_MT_POSITION_X, raw_x) ||
        emit(fd, EV_ABS, ABS_MT_POSITION_Y, raw_y) ||
        emit(fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 32) ||
        emit(fd, EV_KEY, BTN_TOUCH, 1) ||
        emit(fd, EV_SYN, SYN_REPORT, 0);
    /*
     * Keep the synthetic contact down long enough for a busy framebuffer UI
     * loop to observe a PRESSED frame before the RELEASED events arrive.
     */
    usleep(350000);
    failed = failed ||
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1) ||
        emit(fd, EV_KEY, BTN_TOUCH, 0) ||
        emit(fd, EV_SYN, SYN_REPORT, 0);
    if (failed)
        perror("write");
    close(fd);
    return failed ? 1 : 0;
}
