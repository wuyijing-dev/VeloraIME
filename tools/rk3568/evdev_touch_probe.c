#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int send_event(int fd, unsigned short type, unsigned short code, int value)
{
    const struct input_event event = {.type = type, .code = code, .value = value};
    return write(fd, &event, sizeof(event)) == sizeof(event) ? 0 : -1;
}

static int position(int fd, int x, int y)
{
    return send_event(fd, EV_ABS, ABS_X, x)
           || send_event(fd, EV_ABS, ABS_Y, y)
           || send_event(fd, EV_ABS, ABS_MT_POSITION_X, x)
           || send_event(fd, EV_ABS, ABS_MT_POSITION_Y, y)
           || send_event(fd, EV_SYN, SYN_REPORT, 0);
}

static void delay_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    while (nanosleep(&delay, &delay) && errno == EINTR)
        ;
}

static int add_axis(int fd, int axis, int maximum)
{
    const struct uinput_abs_setup setup = {.code = axis, .absinfo = {.maximum = maximum}};
    return ioctl(fd, UI_SET_ABSBIT, axis) < 0 || ioctl(fd, UI_ABS_SETUP, &setup) < 0;
}

static int show_info(const char *device)
{
    const int fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open touchscreen");
        return 1;
    }

    char name[128] = {0};
    struct input_absinfo x_axis, y_axis;
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0
        || ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &x_axis) < 0
        || ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &y_axis) < 0) {
        perror("read touchscreen capabilities");
        close(fd);
        return 1;
    }
    printf("%s x=%d..%d y=%d..%d\n", name, x_axis.minimum, x_axis.maximum,
           y_axis.minimum, y_axis.maximum);
    close(fd);
    return 0;
}

static int virtual_touch(void)
{
    const int fd = open("/dev/uinput", O_WRONLY);
    if (fd < 0) {
        perror("open uinput");
        return 1;
    }
    const struct uinput_setup device = {
        .name = "Velora QA touchscreen",
        .id = {.bustype = BUS_VIRTUAL, .vendor = 0x2310, .product = 0x1490, .version = 1},
    };
    if (ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT) < 0
        || ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0
        || ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH) < 0
        || ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0
        || add_axis(fd, ABS_X, 1079) || add_axis(fd, ABS_Y, 1919)
        || add_axis(fd, ABS_MT_POSITION_X, 1079) || add_axis(fd, ABS_MT_POSITION_Y, 1919)
        || add_axis(fd, ABS_MT_SLOT, 9) || add_axis(fd, ABS_MT_TRACKING_ID, 65535)
        || ioctl(fd, UI_DEV_SETUP, &device) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("register QA touchscreen");
        close(fd);
        return 1;
    }

    delay_ms(1200);
    int failed = 0;
    int x, y, id = 1;
    for (int count = scanf("%d %d", &x, &y); count != EOF;
         count = scanf("%d %d", &x, &y)) {
        if (count != 2 || x < 0 || x >= 1080 || y < 0 || y >= 1920) {
            fprintf(stderr, "Expected touchscreen coordinates inside the portrait display.\n");
            failed = 1;
            break;
        }
        failed = send_event(fd, EV_ABS, ABS_MT_SLOT, 0)
                 || send_event(fd, EV_ABS, ABS_MT_TRACKING_ID, id++)
                 || send_event(fd, EV_KEY, BTN_TOUCH, 1)
                 || position(fd, x, y);
        delay_ms(45);
        failed = send_event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1)
                 || send_event(fd, EV_KEY, BTN_TOUCH, 0)
                 || send_event(fd, EV_SYN, SYN_REPORT, 0) || failed;
        if (failed)
            break;
        delay_ms(30);
    }

    delay_ms(100);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return failed;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[2], "info") == 0)
        return show_info(argv[1]);
    if (argc == 2 && strcmp(argv[1], "virtual") == 0)
        return virtual_touch();
    fprintf(stderr, "Usage: %s DEVICE info | virtual < coordinates.txt\n", argv[0]);
    return 2;
}
