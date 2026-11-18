// SPDX-License-Identifier: Apache-2.0
// Interoperability description of Amazon Linux 4.1.15's userspace mxcfb ABI.
// Source pinned in ports/koa2/source-lock.json, include/uapi/linux/mxcfb.h.
// This is not the PW3 3.0.35 layout. No display operation is performed here.
#pragma once
#include <cstddef>
#include <cstdint>

namespace koa2 {
struct EpdcRect { uint32_t top, left, width, height; };
struct AltBuffer { uint32_t address, width, height; EpdcRect region; };
struct EpdcUpdate {
    EpdcRect region;
    uint32_t waveform, mode, marker;
    int32_t temperature;
    uint32_t flags;
    int32_t ditherMode, quantBit;
    AltBuffer alt;
    uint32_t bwWaveform, grayWaveform, pxpTimestamp, epdcTimestamp;
};
struct Marker { uint32_t value, collision; };
static_assert(sizeof(EpdcRect) == 16, "KOA2 rectangle ABI");
static_assert(sizeof(AltBuffer) == 28, "KOA2 alternate buffer ABI");
static_assert(sizeof(EpdcUpdate) == 88, "KOA2 update ABI");
static_assert(offsetof(EpdcUpdate, temperature) == 28, "KOA2 temperature offset");
static_assert(offsetof(EpdcUpdate, alt) == 44, "KOA2 alternate buffer offset");
static_assert(offsetof(EpdcUpdate, bwWaveform) == 72, "KOA2 histogram offset");
static_assert(sizeof(Marker) == 8, "KOA2 update marker ABI");

// In this kernel TEMP_USE_AUTO aliases TEMP_USE_AMBIENT (PW3 uses 0x1001).
constexpr int32_t TempUseAuto = 0x1000;
constexpr uint32_t WaveformGc16 = 2;
} // namespace koa2

// Host layout checks can run on Windows; ioctl definitions are Linux-only.
#ifdef __linux__
#include <sys/ioctl.h>
namespace koa2 {
constexpr unsigned long SendUpdate = _IOW('F', 0x2E, EpdcUpdate);
constexpr unsigned long WaitUpdate = _IOWR('F', 0x2F, Marker);
} // namespace koa2
#endif
