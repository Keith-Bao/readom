# SELinux policy30 编译研究

来源为 Linux 官方上游的两项 GPL 补丁，保留原始提交和作者信息：

- [671a2781ff01abf4fdc8904881fc3abd3a8279af](https://github.com/torvalds/linux/commit/671a2781ff01abf4fdc8904881fc3abd3a8279af)：ioctl 专用审计字段。
- [fa1aa143ac4a682c7f5fd52a3cf05f5a6fe44a0a](https://github.com/torvalds/linux/commit/fa1aa143ac4a682c7f5fd52a3cf05f5a6fe44a0a)：policy30 与 ioctl 扩展权限。

原始 patch SHA256 固定在 prepare-selinux41.py。应用限制为独立
linux-selinux41 研究树，--fuzz=0、先 dry-run 后应用，不直接修改原厂树。

当前验证：46 项配置检查通过；security/lsm_audit.o、整个 SELinux 子树及
其他 Android 核心对象编译成功。仅应用 xperms 的首轮编译因缺少 ioctl
审计定义失败，加入确切上游前置补丁后通过；没有删掉审计来规避错误。

尚未验证实际 policy30 二进制加载、允许/拒绝 ioctl 的行为及 malformed
policy 的拒绝路径。还需检查后续上游修复，不能把初始补丁等同安全审计完成。
QEMU Binder 测试关闭了 SECURITY，不包含 SELinux 验证。

准备流程：先用 prepare-headless-source.py 解压 linux-selinux41，运行
prepare-selinux41.py，再为 audit-android41-build.sh 设置独立 SOURCE、OUTPUT
和 ARTIFACTS；不设置 Binder EXTRA_CONFIG，因为这是分开的原生 Binder 研究树。
