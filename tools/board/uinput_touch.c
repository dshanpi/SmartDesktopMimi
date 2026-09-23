#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int emit(int fd, unsigned short type, unsigned short code, int value)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    return write(fd, &event, sizeof(event)) == (ssize_t)sizeof(event) ? 0 : -1;
}

static int tap(int fd, int x, int y)
{
    if (x < 0 || x >= 1024 || y < 0 || y >= 768) return -1;
    if (emit(fd, EV_ABS, ABS_MT_SLOT, 0) ||
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 1) ||
        emit(fd, EV_ABS, ABS_MT_POSITION_X, x) ||
        emit(fd, EV_ABS, ABS_MT_POSITION_Y, y) ||
        emit(fd, EV_KEY, BTN_TOUCH, 1) ||
        emit(fd, EV_SYN, SYN_REPORT, 0)) {
        return -1;
    }
    usleep(350000);
    if (emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1) ||
        emit(fd, EV_KEY, BTN_TOUCH, 0) ||
        emit(fd, EV_SYN, SYN_REPORT, 0)) {
        return -1;
    }
    return 0;
}

int main(void)
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        perror("open /dev/uinput");
        return 1;
    }
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) ||
        ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH) ||
        ioctl(fd, UI_SET_EVBIT, EV_ABS) ||
        ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT) ||
        ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID) ||
        ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X) ||
        ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y)) {
        perror("configure uinput");
        close(fd);
        return 1;
    }

    struct uinput_user_dev device;
    memset(&device, 0, sizeof(device));
    snprintf(device.name, sizeof(device.name), "aitvbox-test-touch");
    device.id.bustype = BUS_VIRTUAL;
    device.id.vendor = 0x100a;
    device.id.product = 0x1330;
    device.id.version = 1;
    device.absmin[ABS_MT_SLOT] = 0;
    device.absmax[ABS_MT_SLOT] = 0;
    device.absmin[ABS_MT_TRACKING_ID] = 0;
    device.absmax[ABS_MT_TRACKING_ID] = 65535;
    device.absmin[ABS_MT_POSITION_X] = 0;
    device.absmax[ABS_MT_POSITION_X] = 1023;
    device.absmin[ABS_MT_POSITION_Y] = 0;
    device.absmax[ABS_MT_POSITION_Y] = 767;
    if (write(fd, &device, sizeof(device)) != (ssize_t)sizeof(device) ||
        ioctl(fd, UI_DEV_CREATE)) {
        perror("create uinput device");
        close(fd);
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    puts("READY aitvbox-test-touch");

    int x, y;
    while (scanf("%d %d", &x, &y) == 2) {
        if (tap(fd, x, y)) {
            fprintf(stderr, "invalid tap or write failure: %d %d: %s\n",
                    x, y, strerror(errno));
        } else {
            printf("TAPPED %d %d\n", x, y);
        }
    }

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
