/* SPDX-License-Identifier: GPL-2.0-only */
/* QEMU vexpress-a9 PID 1 smoke combining SELinux policy30 enforcing + multi-context Binder. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include "uapi/binder.h"

static int failures = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("KOA2_ANDROID41_OK %s\n", name);
    } else {
        printf("KOA2_ANDROID41_FAIL %s errno=%d\n", name, errno);
        failures++;
    }
    fflush(stdout);
}

static int load_policy(const char *path) {
    int fd = open(path, O_RDONLY), lfd;
    struct stat st;
    void *buf;
    ssize_t n;
    if (fd < 0 || fstat(fd, &st) < 0) return -1;
    buf = malloc((size_t)st.st_size);
    if (!buf) { close(fd); return -1; }
    if (read(fd, buf, (size_t)st.st_size) != st.st_size) { free(buf); close(fd); return -1; }
    close(fd);
    lfd = open("/sys/fs/selinux/load", O_WRONLY);
    if (lfd < 0) { free(buf); return -1; }
    n = write(lfd, buf, (size_t)st.st_size);
    close(lfd);
    free(buf);
    return (n == st.st_size) ? 0 : -1;
}

static void check_binder_context(const char *dev, const char *tag) {
    int fd = open(dev, O_RDWR | O_CLOEXEC);
    struct binder_version ver = {0};
    void *map;
    char label[64];
    snprintf(label, sizeof(label), "open_%s", tag);
    check(fd >= 0, label);
    if (fd < 0) return;
    map = mmap(NULL, 128 * 1024, PROT_READ, MAP_PRIVATE, fd, 0);
    snprintf(label, sizeof(label), "mmap_%s", tag);
    check(map != MAP_FAILED, label);
    errno = 0;
    snprintf(label, sizeof(label), "version_%s", tag);
    check(ioctl(fd, BINDER_VERSION, &ver) == 0 && ver.protocol_version == BINDER_CURRENT_PROTOCOL_VERSION, label);
    errno = 0;
    snprintf(label, sizeof(label), "set_ctx_mgr_%s", tag);
    check(ioctl(fd, BINDER_SET_CONTEXT_MGR, 0) == 0, label);
    if (map != MAP_FAILED) munmap(map, 128 * 1024);
    close(fd);
}

int main(void) {
    char buf[32];
    int fd, n;
    struct termios tio;
    struct winsize ws;

    mkdir("/proc", 0555); mkdir("/sys", 0555); mkdir("/dev", 0755);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mkdir("/sys/fs/selinux", 0755);
    check(mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", 0, NULL) == 0, "mount_selinuxfs");
    check(load_policy("/policy.30") == 0, "load_policy30");

    fd = open("/sys/fs/selinux/enforce", O_WRONLY);
    check(fd >= 0 && write(fd, "1", 1) == 1, "set_enforcing");
    if (fd >= 0) close(fd);
    fd = open("/sys/fs/selinux/enforce", O_RDONLY);
    memset(buf, 0, sizeof(buf));
    if (fd >= 0) { n = (int)read(fd, buf, sizeof(buf) - 1); close(fd); } else n = -1;
    check(n > 0 && buf[0] == '1', "enforcing_active");

    /* Multi-context Binder under SELinux policy30 enforcing mode */
    check_binder_context("/dev/binder", "binder");
    check_binder_context("/dev/hwbinder", "hwbinder");
    check_binder_context("/dev/vndbinder", "vndbinder");

    /* SELinux ioctl xperms enforcement still active alongside Binder */
    fd = open("/dev/console", O_RDWR);
    check(fd >= 0, "open_console");
    if (fd >= 0) {
        errno = 0;
        check(ioctl(fd, TCGETS, &tio) == 0, "allowed_ioctl_tcgets");
        errno = 0;
        check(ioctl(fd, TIOCGWINSZ, &ws) < 0 && errno == EACCES, "denied_ioctl_tiocgwinsz_eacces");
        close(fd);
    }

    if (failures == 0)
        printf("KOA2_ANDROID41_SMOKE_PASS failures=0\n");
    else
        printf("KOA2_ANDROID41_SMOKE_FAIL failures=%d\n", failures);
    fflush(stdout);
    reboot(RB_POWER_OFF);
    for (;;) pause();
}
