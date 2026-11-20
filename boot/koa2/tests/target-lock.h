/* SPDX-License-Identifier: GPL-2.0-only */
/* Test lock for boot/koa2/tests/test-stage.c: the smoke footprint, with the
 * pinned zImage replaced by 6652856 zero bytes so the test can make one. */
/* (generated from target-lock.h.example)
 * was built for. ports/koa2/build-koa2-module.py generates the real header
 * from the kernel it just built; this example is the headless-smoke build, so
 * the file compiles and the tests have something to read. */
#ifndef KOA2_TARGET_LOCK_H
#define KOA2_TARGET_LOCK_H
/* uname of the stock kernel the module is inserted into. */
#define K2S_STOCK_RELEASE "4.1.15-lab126"
#define K2S_STOCK_MACHINE "armv7l"
#define K2S_STOCK_VERSION "#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023"
/* The new kernel this build carries (ports/koa2/derive-kernel-footprint.py). */
#define K2S_KERNEL_BYTES 6652856u
#define K2S_KERNEL_SHA256 {0x99,0x76,0xa0,0xf1,0x50,0xaf,0xdb,0xe4,0x87,0xa0,0x84,0xc7,0xe8,0x5b,0x9d,0xb6,0x2f,0x01,0xc7,0xe4,0x26,0x47,0x37,0xe5,0x36,0xb7,0x68,0x4d,0xbe,0x6f,0x3f,0x9b}
#define K2S_ZRELADDR 0x80008000u
#define K2S_KERNEL_END 0x80c55a0cu
#define K2S_ZIMAGE_RUNTIME 6722520u
/* The command line the new kernel is given. */
#define K2S_BOOTARGS "console=ttymxc0,115200 no_console_suspend rdinit=/init koa2_smoke_hold=20 koa2_smoke_wdt=30"
#define K2S_HAS_INITRAMFS 1
#endif
