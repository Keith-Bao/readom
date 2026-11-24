/* SPDX-License-Identifier: GPL-2.0-only */
/* QEMU vexpress-a9 PID 1 smoke for SELinux policy30 + ioctl xperms. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static int failures = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("KOA2_SELINUX30_OK %s\n", name);
    } else {
        printf("KOA2_SELINUX30_FAIL %s errno=%d\n", name, errno);
        failures++;
    }
    fflush(stdout);
}

static int load_policy_file(const char *path) {
    int fd = open(path, O_RDONLY);
    struct stat st;
    void *buf;
    int load_fd;
    ssize_t n;
    if (fd < 0 || fstat(fd, &st) < 0) return -1;
    buf = malloc((size_t)st.st_size);
    if (!buf) { close(fd); return -1; }
    if (read(fd, buf, (size_t)st.st_size) != st.st_size) {
        free(buf); close(fd); return -1;
    }
    close(fd);
    load_fd = open("/sys/fs/selinux/load", O_WRONLY);
    if (load_fd < 0) { free(buf); return -1; }
    n = write(load_fd, buf, (size_t)st.st_size);
    close(load_fd);
    free(buf);
    return (n == st.st_size) ? 0 : -1;
}

int main(void) {
    char buf[64];
    int fd, n;
    struct winsize ws;
    struct termios tio;

    mkdir("/proc", 0555); mkdir("/sys", 0555); mkdir("/dev", 0755);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mkdir("/sys/fs/selinux", 0755);
    check(mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", 0, NULL) == 0, "mount_selinuxfs");

    fd = open("/sys/fs/selinux/policyvers", O_RDONLY);
    memset(buf, 0, sizeof(buf));
    if (fd >= 0) { n = (int)read(fd, buf, sizeof(buf) - 1); close(fd); } else n = -1;
    check(n > 0 && atoi(buf) == 30, "policyvers_is_30");

    /* Malformed policy must be rejected before loading the real policy. */
    errno = 0;
    check(load_policy_file("/policy.bad") < 0, "malformed_policy_rejected");

    /* Valid policy30 with allowxperm must load cleanly. */
    check(load_policy_file("/policy.30") == 0, "valid_policy30_loaded");

    /* Switch to enforcing mode and confirm readback. */
    fd = open("/sys/fs/selinux/enforce", O_WRONLY);
    check(fd >= 0 && write(fd, "1", 1) == 1, "set_enforcing");
    if (fd >= 0) close(fd);
    fd = open("/sys/fs/selinux/enforce", O_RDONLY);
    memset(buf, 0, sizeof(buf));
    if (fd >= 0) { n = (int)read(fd, buf, sizeof(buf) - 1); close(fd); } else n = -1;
    check(n > 0 && buf[0] == '1', "enforcing_readback_1");

    /* Test ioctl xperms on /dev/console (chr_file): TCGETS (0x5401) allowed, TIOCGWINSZ (0x5413) denied. */
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
        printf("KOA2_SELINUX30_SMOKE_PASS failures=0\n");
    else
        printf("KOA2_SELINUX30_SMOKE_FAIL failures=%d\n", failures);
    fflush(stdout);
    reboot(RB_POWER_OFF);
    for (;;) pause();
}
