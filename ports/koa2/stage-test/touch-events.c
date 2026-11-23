// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct touch_frame {
    bool contact;
    bool dropped;
};

static bool touch_reported(struct touch_frame *frame, const struct input_event *event)
{
    if (event->type == EV_SYN && event->code == SYN_DROPPED) {
        frame->dropped = true;
        frame->contact = false;
    } else if (event->type == EV_SYN && event->code == SYN_REPORT) {
        bool reported = frame->contact;

        *frame = (struct touch_frame){0};
        return reported;
    } else if (!frame->dropped &&
               ((event->type == EV_ABS && event->code == ABS_MT_TRACKING_ID && event->value >= 0) ||
                (event->type == EV_KEY && event->code == BTN_TOUCH && event->value == 1))) {
        frame->contact = true;
    }
    return false;
}

static int64_t monotonic_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static const char *observe_touch(int fd)
{
    struct pollfd reader = {.fd = fd, .events = POLLIN};
    struct touch_frame frame = {0};
    int64_t start = monotonic_ms();

    if (start < 0)
        return "touch_clock_error";
    for (;;) {
        struct input_event events[16];
        int64_t now = monotonic_ms();
        ssize_t count;
        int ready;

        if (now < 0)
            return "touch_clock_error";
        if (now - start >= 10000)
            return "touch_no_contact_observed";
        ready = poll(&reader, 1, (int)(10000 - (now - start)));
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready == 0)
            return "touch_no_contact_observed";
        if (ready < 0 || (reader.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return "touch_read_failed";
        count = read(fd, events, sizeof(events));
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (count <= 0 || count % (ssize_t)sizeof(events[0]) != 0)
            return "touch_read_failed";
        for (size_t i = 0; i < (size_t)count / sizeof(events[0]); ++i) {
            if (touch_reported(&frame, &events[i]))
                return "touch_contact_observed";
        }
    }
}

int main(int argc, char **argv)
{
    char name[64] = {0};
    const char *result;
    int fd;

    if (argc != 2)
        return 2;
    fd = open(argv[1], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        puts("touch_open_failed");
        return 1;
    }
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
        strncmp(name, "cyttsp5_mt", sizeof(name)) != 0)
        result = "touch_wrong_device";
    else
        result = observe_touch(fd);
    close(fd);
    puts(result);
    return strcmp(result, "touch_contact_observed") != 0;
}
