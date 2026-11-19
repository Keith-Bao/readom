/* SPDX-License-Identifier: GPL-2.0-only */
/* PID 1 for the KOA2 headless-smoke kernel. It never opens a block device,
 * never mounts storage and never calls reboot(): after a bounded hold it stops
 * feeding /dev/watchdog without closing it, so the i.MX watchdog resets the
 * board back into the stock bootloader (ports/koa2/smoke-initramfs.md). */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include "smoke-params.h"
#include <unistd.h>

static int kmsg = -1;

static void say(const char *format, ...) {
    char line[256];
    int n;
    va_list ap;
    va_start(ap, format);
    n = vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(line)) n = sizeof(line) - 1;
    /* Console for QEMU and the serial pad; /dev/kmsg so the line lands in the
     * Lab126 printk buffer that the stock kernel exposes as /proc/printkbuf. */
    dprintf(1, "KOA2_SMOKE %s\n", line);
    if (kmsg >= 0) dprintf(kmsg, "<5>KOA2_SMOKE %s\n", line);
}

static void read_line(const char *path, char *out, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;
    snprintf(out, size, "unavailable");
    if (fd < 0) return;
    n = read(fd, out, size - 1);
    close(fd);
    if (n <= 0) { snprintf(out, size, "unavailable"); return; }
    out[n] = 0;
    out[strcspn(out, "\n")] = 0;
}

static void report(void) {
    struct utsname u;
    char text[256], names[200] = "";
    DIR *d;
    struct dirent *e;
    if (!uname(&u)) say("release=%s machine=%s", u.release, u.machine);
    read_line("/sys/devices/system/cpu/online", text, sizeof(text));
    say("cpu_online=%s", text);
    {
        FILE *f = fopen("/proc/meminfo", "r");
        if (f) { if (fgets(text, sizeof(text), f)) { text[strcspn(text, "\n")] = 0; say("%s", text); } fclose(f); }
    }
    /* Names only: proves the standard eMMC driver bound without touching the device. */
    d = opendir("/sys/block");
    if (d) {
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || !strncmp(e->d_name, "ram", 3) || !strncmp(e->d_name, "loop", 4)) continue;
            if (strlen(names) + strlen(e->d_name) + 2 < sizeof(names)) { strcat(names, e->d_name); strcat(names, " "); }
        }
        closedir(d);
    }
    say("block_devices=%s", names[0] ? names : "none");
}

int main(void) {
    char cmdline[1024];
    unsigned hold, timeout, i;
    int wdt, t;
    mkdir("/proc", 0555); mkdir("/sys", 0555); mkdir("/dev", 0755);
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RDONLY, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID | MS_NOEXEC, NULL);
    kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    read_line("/proc/cmdline", cmdline, sizeof(cmdline));
    hold = koa2_smoke_parameter(cmdline, "koa2_smoke_hold=", 20, 1, 50);
    timeout = koa2_smoke_parameter(cmdline, "koa2_smoke_wdt=", 30, 2, 60);
    say("start pid=%d hold=%u wdt=%u", (int)getpid(), hold, timeout);
    report();
    wdt = open("/dev/watchdog", O_WRONLY | O_CLOEXEC);
    if (wdt < 0) {
        /* Without the device the kernel's own ping timer keeps the board alive;
         * staying here would never return to stock, so say so loudly and wait. */
        say("FAIL watchdog_open errno=%d; no automatic return, hold power key", errno);
        for (;;) pause();
    }
    t = (int)timeout;
    if (ioctl(wdt, WDIOC_SETTIMEOUT, &t) || ioctl(wdt, WDIOC_GETTIMEOUT, &t))
        say("watchdog_timeout_ioctl errno=%d", errno);
    say("watchdog_owned timeout=%d", t);
    for (i = 0; i < hold; i++) {
        ioctl(wdt, WDIOC_KEEPALIVE, 0);
        if (i % 5 == 0) say("alive t=%u", i);
        sleep(1);
    }
    say("PASS released_watchdog expect_reset_within=%d", t);
    /* Do not close wdt: closing without the magic 'V' hands feeding back to the kernel timer. */
    for (;;) pause();
}
