# Linux 4.1 Android 接口编译审计

## 原生接口

执行 audit-android41-build.sh：46 项请求配置经 olddefconfig 后逐项通过。
seccomp、namespace、SELinux 子树、原生 Binder、ashmem、LMK 对象均已成功编译。
没有生成 Android 安装包，也未做完整内核链接或运行时测试。

原生 Binder 只有一个 context；SELinux include/security.h 的策略上限为
POLICYDB_VERSION_CONSTRAINT_NAMES（29）。Android 8.1 仍需要多 context、
SG 事务和策略版本 30/xperms 等适配。不能以原生对象编译成功宣称已经支持。

## Binder 研究实现

reconstruct-binder.py 从仓库既有 0001、0003、0900、0901 补丁中只提取
Binder 源文件相关 section，重建带多 context/SG、poll 生命周期和私有
buffer metadata 修复的源码快照；原始补丁与输出均有 SHA256 回执。
原始来源/许可证沿用 patches/android-core 中的溯源和文件头。

不应用 0008 的 3.0 fdtable 改动。wire-binder41.py 仅作用于独立
linux-binder41 树，适配以下 4.1 API：

- FD 分配/安装/关闭直接使用官方 4.1 Binder 的辅助实现，避免修改 fdtable。
- 内核身份保持 kuid_t，比较使用 uid_eq，传给用户空间才做 from_kuid 转换。
- PID namespace 使用 task_active_pid_ns；补入声明头文件。
- hlist 迭代使用 4.1 参数形式。
- map_vm_area 第三个参数使用 page 指针数组，不传错误的三级指针。

此研究实现及原生安全对象已编译成功，46+1 项配置校验通过。
剩余已见警告为原厂 percpu-refcount.h 的未使用静态变量；未通过屏蔽
类型/隐式函数错误来通过编译。运行时事务、引用计数、context 隔离、
FD 传递及安全行为尚未验证，不可部署。

## 可重现命令

在 WSL 的 builder 用户下：

```bash
REPO=/mnt/f/Kindle/readom
python3 "$REPO/ports/koa2/reconstruct-binder.py" "$HOME/readom-work/binder-research-new"
python3 "$REPO/ports/koa2/prepare-headless-source.py" "$HOME/readom-work/linux-4.1.15.tar.gz" "$HOME/readom-work/linux-binder41"
python3 "$REPO/ports/koa2/wire-binder41.py" "$HOME/readom-work/binder-research-new" "$HOME/readom-work/linux-binder41"
export KOA2_AUDIT_SOURCE="$HOME/readom-work/linux-binder41"
export KOA2_AUDIT_OUTPUT="$HOME/readom-work/out-binder41-audit"
export KOA2_AUDIT_ARTIFACTS="$HOME/artifacts/binder41-audit"
export KOA2_AUDIT_EXTRA_CONFIG="$REPO/ports/koa2/binder41.config"
bash "$REPO/ports/koa2/audit-android41-build.sh"
```

重建目录必须为空，防止覆盖研究修改。wire 工具拒绝未知的驱动修改，只允许
原始、精确快照、当前生成内容及一个已知旧适配哈希。所有对象在独立输出目录。
下一步为 QEMU 内核/用户空间 smoke，验证真实 ioctl 与三 context 的隔离。

## 2026-11-24 合并构建与 QEMU 联合实测（Binder + SELinux enforcing）

已在独立研究树 `linux-android41` 合并 `prepare-headless-source.py`（标准 eMMC、
`dump_lastk_to_mmc` 空操作、printk D-cache 回写）、多 context Binder（`binder41.config`）、
SELinux `policy30`/`ioctl xperms` 与 `android41-core.config`（46+1 项配置全过）：

- 合并编译时发现 `imx_v7_zelda_defconfig` 原先带着 i.MX7D 并不存在的 `CONFIG_MXC_GPU_VIV=y`，
  在开启 `CONFIG_ANDROID=y` 后会因缺少 `<sync.h>` 编断；`build-android41-kernel.sh`
  已禁用 `MXC_GPU_VIV` 与 `FALCON_WRAPPER`，完整编出 KOA2 `zImage`、`imx7d-zelda.dtb` 与全部模块。
- `build-qemu-android41.sh` / `run-qemu-android41.py` 在 QEMU `vexpress-a9` 双核下同时开启
  **SELinux `policy30` `enforcing=1` + 三 context Binder** 实跑通过
  （`KOA2_ANDROID41_SMOKE_PASS failures=0`，0 告警/panic）：`/dev/binder`、`/dev/hwbinder`、
  `/dev/vndbinder` 均在 enforcing 下完成 `mmap`、`BINDER_VERSION` 与 `BINDER_SET_CONTEXT_MGR`，
  同时 `TIOCGWINSZ`（`0x5413`）被 `xperms` 拒绝（`-EACCES` + `ioctlcmd=5413` 审计）。
