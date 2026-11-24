/* SPDX-License-Identifier: Apache-2.0 */
/* Kindle Oasis 2 (KOA2 / i.MX7D) Power HAL: frontlight blanking, EPDC blanking
 * fallback, and cpufreq launch boost. */
#include <hardware/power.h>
#include <linux/fb.h>
#include <log/log.h>
#include <cutils/properties.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define KOA2_BL_POWER "/sys/class/backlight/max77696-bl/bl_power"
#define KOA2_CPUFREQ "/sys/devices/system/cpu/cpu0/cpufreq/"
static char cpu_min[16], cpu_max[16];

static int composer_owns_epdc(void) {
    char name[PROPERTY_VALUE_MAX];
    property_get("ro.hardware.hwcomposer", name, "");
    return strcmp(name, "koa2") == 0;
}

static int read_line(const char *path, char *buf, size_t sz) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n;
    do { n = read(fd, buf, sz - 1); } while (n < 0 && errno == EINTR);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return 0;
}

static void write_str(const char *path, const char *val) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;
    size_t len = strlen(val);
    ssize_t n;
    do { n = write(fd, val, len); } while (n < 0 && errno == EINTR);
    close(fd);
}

static void koa2_set_interactive(struct power_module *module, int on) {
    (void)module;
    write_str(KOA2_BL_POWER, on ? "0\n" : "4\n");
    if (composer_owns_epdc()) return;
    int fd = open("/dev/graphics/fb0", O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        int rc;
        do { rc = ioctl(fd, FBIOBLANK, on ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN); }
        while (rc < 0 && errno == EINTR);
        close(fd);
    }
}

static void koa2_power_init(struct power_module *module) {
    (void)module;
    if (read_line(KOA2_CPUFREQ "cpuinfo_min_freq", cpu_min, sizeof(cpu_min)) < 0 ||
        read_line(KOA2_CPUFREQ "cpuinfo_max_freq", cpu_max, sizeof(cpu_max)) < 0) {
        cpu_max[0] = '\0';
    }
}

static void koa2_power_hint(struct power_module *module, power_hint_t value, void *data) {
    (void)module;
    if (value != POWER_HINT_LAUNCH || !cpu_max[0]) return;
    write_str(KOA2_CPUFREQ "scaling_min_freq",
              (data && *(const int32_t *)data) ? cpu_max : cpu_min);
}

struct power_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = POWER_MODULE_API_VERSION_0_2,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = POWER_HARDWARE_MODULE_ID,
        .name = "KOA2 EPDC, frontlight, and cpufreq power HAL",
        .author = "Readom KOA2 port",
    },
    .init = koa2_power_init,
    .setInteractive = koa2_set_interactive,
    .powerHint = koa2_power_hint,
};
