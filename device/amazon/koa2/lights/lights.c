/* SPDX-License-Identifier: Apache-2.0 */
/* Kindle Oasis 2 (KOA2 / MAX77796) 12-bit monochrome frontlight HAL. */
#include <hardware/lights.h>
#include <log/log.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../hardware/PowerContract.h"

#define KOA2_BL_PATH "/sys/class/backlight/max77796-bl/brightness"

static int set_light_backlight(struct light_device_t *device,
                               const struct light_state_t *state) {
    (void)device;
    if (!state || state->flashMode != LIGHT_FLASH_NONE) return -EINVAL;
    unsigned color = state->color;
    unsigned level = (77u * ((color >> 16) & 255u) +
                      150u * ((color >> 8) & 255u) +
                      29u * (color & 255u) + 128u) >> 8;
    int32_t hw = koa2_brightness_android_to_hw((int32_t)level);
    char text[16];
    int count = snprintf(text, sizeof(text), "%d\n", (int)hw);
    int fd = open(KOA2_BL_PATH, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    ssize_t written;
    do { written = write(fd, text, (size_t)count); } while (written < 0 && errno == EINTR);
    int status = written == count ? 0 : (written < 0 ? -errno : -EIO);
    close(fd);
    return status;
}

static int close_light(struct hw_device_t *device) {
    free(device);
    return 0;
}

static int open_light(const struct hw_module_t *module, const char *name,
                      struct hw_device_t **out) {
    if (!out || !name || strcmp(name, LIGHT_ID_BACKLIGHT)) return -EINVAL;
    struct light_device_t *dev = calloc(1, sizeof(*dev));
    if (!dev) return -ENOMEM;
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = LIGHTS_DEVICE_API_VERSION_1_0;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = close_light;
    dev->set_light = set_light_backlight;
    *out = &dev->common;
    return 0;
}

static struct hw_module_methods_t methods = { .open = open_light };

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = HARDWARE_MODULE_API_VERSION(1, 0),
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "KOA2 12-bit monochrome frontlight",
    .author = "Readom KOA2 port",
    .methods = &methods,
};
