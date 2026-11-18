# Binder 的 QEMU 运行验证

硬件：仅 QEMU vexpress-a9，两个 Cortex-A9、512MiB，无磁盘、无网络、无图形窗口。
内核来自隔离 linux-binder41 研究树；不能用于 Kindle。

## 可重现步骤

```bash
bash /mnt/f/Kindle/readom/ports/koa2/build-qemu-binder.sh
python3 /mnt/f/Kindle/readom/ports/koa2/run-qemu-binder.py
```

run 脚本限制 60 秒，保存 runtime.log；除成功标志外，还拒绝 WARNING、BUG、
Oops 和 panic。无磁盘后端，不涉及设备分区。

当前通过的实际内核 ioctl 测试：

- binder/hwbinder/vndbinder 三设备，32 位协议版本 7。
- 每设备只读 mmap 成功，独立设置 context manager。
- 第二个进程申请各 context 管理权均被 EBUSY 拒绝。
- 子进程向三个 context 分别发送不同的 one-way 事务；父进程接收并验证
  code 和实际映射中的 payload，随后 BC_FREE_BUFFER 回收缓冲区。
- 正常关闭映射与 FD，输出 KOA2_BINDER_SMOKE_PASS failures=0 并关闭模拟器。

首轮模拟器实测捕获了 Binder 页映射告警：旧 tmp_area 的 guard 处理与 4.1
不兼容。现移植官方 4.1 的 map_kernel_range_noflush + flush_cache_vmap
路径并检查返回映射页数为 1；重跑后上述测试通过，未见内核告警。

为了让 Amazon 内核在 vexpress 上构建，QEMU 专用配置关闭缺依赖的 MMA8451
和 LAB126_PRINTK_BUFFER；prepare-qemu-source.py 修复原厂日志代码遗漏的
CONFIG_LAB126_PRINTK_BUFFER guard。此改动仅在研究树，不改 linux-stock。

尚未验证：同步回复、FD/PTR/FDA/SG 传递、并发/退出压力、SELinux 策略、
Android framework、i.MX7 驱动和任何 Kindle 真机行为。
