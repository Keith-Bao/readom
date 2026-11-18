# 路线审计结论：继续新内核，排除只补模块的完整适配

## 原厂内核路线

真实采集 v2/kernel.config：NAMESPACES=n、SECCOMP=n、SECURITY=n、ANDROID=n。
CONFIG_SECURITYFS=y 不等于具备 LSM 或 SELinux。

官方源码证据：init/Kconfig 的 NAMESPACES、arch/arm/Kconfig 的 SECCOMP、
security/Kconfig 的 SECURITY、security/selinux/Kconfig 的 SECURITY_SELINUX
均为 bool。seccomp 还改变 fork.c 中编译期任务处理；安全框架在内核构建时
选择 security.o 和 SELinux，而非一个可后装的常规模块。

因此仅以外部模块补 Binder/ashmem，不能补齐既有运行内核省略的 namespace、
seccomp 和安全框架，无法满足本项目的完整 Android 和 SELinux enforcing 目标。
Binder/ashmem 自身也不是当前源码里可直接 make=m 的现成驱动；其模块化还需改造。
不采用内核热补丁、劫持系统调用或关闭 Android 安全检查作为绕过办法。

此前将原厂内核作为优先候选只是待审计的假设，现排除该完整适配路线。
这不否定原厂系统运行普通 Linux 程序，也不证明所有定制 Android 实验都不可能；
这里只对本项目已约定的功能和安全验收负责。

## EPDC 缺口的具体调用边界

当前 4.1.15 drivers/video/fbdev/mxc/mxc_epdc_v2_fb.c 的直接引用：

| 接口 | 调用位置（原始源码行） | 需要核实/恢复的行为 |
|---|---|---|
| read_mnt_wfm_v2 | 5453、6584 | 本机波形读取、长度与所有权、错误处理 |
| eink_get_wfm_voltage_control | 5522、6669 | VCD/ACD 解析与波形电压数据 |
| fetch_epdc_vdc_04_data | 1983、5525、6671 | 模式/温度索引与 VDC 数据 |
| fetch_epdc_adc_06_data | 2876、2907、6675、6679 | 更新期间使用的 ACD 数据 |
| mxc_epdc_waveform_proc | 6663 | 波形诊断节点和资源生命周期 |
| mxc_epdc_waveform_init | 6703 | 面板波形初始化、VCOM 等配置 |
| mxc_epdc_panel_init | 7602 | flash 供电后面板初始化 |
| wfm_name_for_mode | 3892 等 | 日志中的波形名称 |

这是已核实的直接边界，**不是完整的缺失符号清单**；包含文件也可能定义
主文件其他位置使用的宏/类型/全局量，仍须做完整预处理和链接级验证。

旧 3.0.35 参考只有部分同名实现。尤其 eink_get_wfm_voltage_control 的旧签名
只有两个参数，4.1 调用传入三个参数，不能直接拼接。
旧参考缺少 read_mnt_wfm_v2、fetch_epdc_vdc_04_data、fetch_epdc_adc_06_data
这些同名入口（固定参考提交见 epdc-source-gap.md）。电压部分不是可忽略的日志。

## 实施方向

1. 新内核仍为主路线，继续寻找完整的 4.1 Lab126 显示实现。
2. 找不到兼容源码时，先从官方公开结构、现有调用点和旧 GPL 算法恢复
   波形格式与电压控制契约，并编写有边界检查的纯主机解析器。
3. 未掌握本机波形格式、电压量纲和调度语义前，不尝试显示/供电真机写操作。
4. 替代实现需独立标记为 KOA2 开发内核；不能称为原厂内核复现，原计划 C
   的“原厂基线构建通过”只能记录为因公开源码不完整未实现。
5. Android 通用接口研究可在主机进行，但不跨过 SMP/DTB 交接和恢复验证门槛。

audit-stock-android.py 可对采集配置重复执行；退出 2 表示存在已检查的
编译期障碍。输出不等于完整 Android 兼容性测试。
