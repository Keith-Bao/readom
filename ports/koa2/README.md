# Oasis 2 本地移植工作区

状态：硬件信息采集阶段，尚不能启动 Android。不能使用 PW3 安装包。

首次采集已完成，实机结论见 [hardware-findings.md](hardware-findings.md)，
官方内核源码校验信息见 [source-lock.json](source-lock.json)。
采集器 v2 修正了 KOA2 存储设备 mmcblk2 的分区属性采集。

## 已从这台设备确认

- USB 存储卷：D:，约 6.67 GB（十进制）。
- `system/version.txt`：Kindle 5.16.2.1.1 (409745 002)。
- KOReader 日志：KindleOasis2，v2026.07.1，1264×1680。
- 输入设备：cyttsp5_mt 触摸、gpio-keys 翻页键、bma2x2 加速度计、
  bma_interrupt 旋转事件、max77796-key、电源键、两个霍尔传感器。
- 根目录 VERSION 是越狱组件版本，不能当作固件版本。

## 首次采集

`kual/` 复制到 Kindle 的 `extensions/readom-koa2-probe/`。
安全弹出 USB，断线，在 Kindle 打开 KUAL → Readom Oasis 2 Probe →
Collect hardware report。等待约 30 秒后重新连接电脑。

报告保存在 Kindle 根目录 `koa2-port-probe/report.txt`；如内核允许，另有
`kernel.config`；`complete.txt` 表示已完成。本脚本沿用上游只读硬件采集器，
仅写入该报告目录，不读取账户和书籍，不联网，不加载模块，不修改分区或硬件设置。

## 接下来依赖真实设备信息的工作

1. 核实运行内核、设备树兼容字符串、内存和保留区域、EPDC、背光、供电及输入节点。
2. 获取 Oasis 2 固件对应的 Amazon GPL 源码，确定板级 defconfig；
   上游记载本机内核为 4.1.15，不能直接应用面向 3.0.35 的全部补丁。
3. 按功能核对 Android 8.1 所需 binder、ashmem、SELinux、seccomp 等内核配置，
   分离已有功能与需要移植的补丁，建立 Linux 构建环境后编译验证。
4. 为 4.1 内核重新验证启动交接，采集必要的启动布局和函数校验信息；
   不照抄 PW3 profile 中的地址或哈希，不绕过安装器机型检查。
5. 建立独立 AOSP 产品；适配屏幕方向、实体翻页键、旋转、触摸、
   MAX77796 相关驱动（具体供电芯片仍待核实）、Wi-Fi、休眠和 USB。
6. 先做受控的内核启动验证，再做 Android 首启、回退和长时间稳定性验证。

上游依据：`docs/design/porting-to-other-models.md`、`boot/README.md`。
本机尚未配置 WSL/Linux 构建环境；下载本仓库不等于已有完整 AOSP 和内核源码。
