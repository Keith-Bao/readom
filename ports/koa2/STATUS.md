# 当前执行状态

## 第二台主机重建（2026-11-19，覆盖下文的主机路径记录）

- 新主机 WSL 环境已建立，细节见 build-environment.md「第二台主机」。
- headless-smoke 在本机从源码重建：make 退出 0，
  release=4.1.15-lab126-koa2-headless-smoke，97 个模块 vermagic 全部匹配
  （identity.json passed）。日志 ~/artifacts/headless-build-this-host.log。
- DTB SHA256 与上一台主机一致：8df4fe5d…f144d9。
- zImage SHA256 为 71893c8e…eacbc6，与上一台主机记录的 4c69f47e…3de9715f 不同。
  未压缩 Image 为 27a6062c…efbc0（上一台主机未记录 Image 哈希，无法逐层对照）。
  原因尚未查明，不能宣称两台主机产物可复现一致；两者都不可部署。
- 主机回归：50 passed、0 failed、24 not run here，与上一台主机相同。
  日志 ~/artifacts/host-tests-this-host.log。
- 只读 Boot Preflight 已由用户在 Kindle 上运行（20261119-044638-4937），结果见
  boot-preflight-findings.md：可用 RAM 两段、EPDC/ramoops/printk/memreserve 保留区、
  2G/2G 且 lowmem 恒等、运行时 DTB 与编译 DTB 的板级差异、原厂会直接写 MMC 原始扇区。
  原始数据只在 ~/koa2-evidence/，未进仓库。
- 排除表已写进 boot/koa2/contract.c（koa2_measured_ram/excluded，7 段），并新增运行时 FDT
  memreserve 读取与"每个 memreserve 必须被排除表覆盖"的检查；新的 bootloader 保留会被拒绝而不是覆盖。
  ASan/UBSan 主机测试、qemu-arm 静态测试、运行时 DTB 与编译 DTB 全部通过。
- memreserve #1 已查明是 FDT 本身（U-Boot fdt_shrink_to_minimum）；审计中另发现 Falcon BIOS
  0x9ffc0000–0x9fffffff 必须保留，已补进排除表。
- __dump_lastk_to_mmc 的三个调用方与目标扇区已审计（见 boot-preflight-findings.md 末节）：
  原厂每次经看门狗重启都会写 eMMC 扇区 0x9a400 起 256 KiB。交接不得走重启路径。
- 仍没有任何新内核上机，交接仍只有校验、没有跳转。下一步（主机侧）：
  核实 mmcblk2 能否用标准 sdhci 驱动访问，从而让实验内核去掉 FALCON_BLK；
  设计次核下线与跳转前的静止顺序。


## 最新进度（覆盖下文早期 WSL 阻塞记录）

- 当前完整主机回归：50 passed、0 failed、24 not run here。
  日志 /home/builder/artifacts/host-tests-current.log；Linux 副本已从 Git
  快进到 7772e24，避免 Windows CRLF 覆盖 shell 文件。
- 原始上游 GPL 补丁中的 3 个公开 sign-off 邮箱已按两个确切文件路径
  加入既有隐私扫描允许清单；没有放宽其他文件或设备数据规则。
- 本轮已自动提交主机契约、预采集包、Binder 4.1 适配及 QEMU IPC、SELinux
  policy30 编译研究。后续自主工作优先为 SELinux 策略运行测试与完整集成准备。

- QEMU Binder 继续扩展并通过：同步请求/回复、实际 FD 数据读取、
  BC_TRANSACTION_SG 的 PTR 缓冲区重定位与数据完整性；无内核告警。

- QEMU 双核内核运行验证已通过：三 Binder context 独立管理、协议/mmap、
  子进程实际 one-way payload 分发与回收，运行日志没有 WARNING/BUG/Oops/panic。
  测试曾捕获页映射 bug，已改为官方 4.1 映射路径。见 qemu-binder.md。
- SELinux policy30/xperms 与 ioctl 审计前置补丁来自官方固定提交，独立树
  对象编译通过；策略加载与执行行为未验证。见 selinux41.md。
- 原厂 printk 的 dump_lastk_to_mmc 包含原始 MMC 写入入口。设备实验前
  必须审计/隔离此路径；当前没有任何新内核上机。
- 当前下一步：扩大 Binder 同步/FD/SG 运行测试；policy30 行为测试；
  真机后端仍等待原厂运行时 FDT/保留内存的只读报告，不能猜地址直接跳转。

- 主机自主工作新进展：android41-core.config 46 项原生配置全部生效，
  namespace/seccomp/SELinux/ashmem/原生 Binder/LMK 对象编译通过。
- 已从仓库的可追溯补丁重建 Binder 多 context/SG/安全修复实现，针对 4.1
  FD、kuid、PID namespace、hlist、map_vm_area 做适配；46+1 配置和对象编译通过。
  未做 Binder 运行时验证，尚未支持 SELinux policy30。见 android41-audit.md。
- 下一项可独立工作：QEMU 中建立真实 Binder 三 context 隔离及 ioctl smoke；
  同时真机交接仍等待用户方便时运行已部署的只读 Boot Preflight。

- 已新增 boot/koa2/contract.c：只读 FDT 与内存区间校验；ASan/UBSan 下
  截断/越界/机型/区间测试及 20000 变异测试通过；ARM 静态版 QEMU 也通过。
- KUAL 的 Readom KOA2 Boot Preflight 已准备并部署，未由代理执行。
  用户方便时启动一次，报告保存在 koa2-boot-preflight/；原始数据不可上传 Git。
- 编译 DTB 缺 bootargs，内存比实机可用范围宽，不能直接拿来交接。
  真实交接前必须取得并核验原厂运行时 FDT、保留范围和日志行为。
- 用户睡觉期间继续主机开发/测试/自动提交，所有设备跳转仍未执行。

- 最新：实验源码已独立解压到 linux-headless-smoke，原厂 linux-stock 不变。
  setlocalversion 条件修正只识别实验后缀；4 项实际 shell 隔离测试通过。
- 已重建成功，release=4.1.15-lab126-koa2-headless-smoke，97 个模块的
  ELF .modinfo vermagic 全部匹配。保守 ARM 重定位检查通过，未做实机加载。
- zImage SHA256：4c69f47ef82d26ef76d2b1701bc7b32de9715f528fc899ba3124e36140de3f6e。
- 日志：/home/builder/artifacts/headless-identity-build.log、headless-identity.json、
  headless-relocations.json。旧原厂后缀产物仍仅作历史证据，不可部署。
- 当前下一步：实现 DTB/内存契约的主机校验与只读设备校验；核实 stock
  模块 ABI、SMP 次核退出、DMA/中断静止和日志保留后才准备跳转实验。
- 原厂 cpu_down 有导出，hotplug 的 i.MX7D 路径包含 GPCv2 次核电源控制；
  只读源码审计不等于次核可以安全下线或支持恢复。尚无可用交接模块。

- 当前主路线已按用户要求改为 headless-smoke：先解决无显示新内核和交接。
- 独立 profile 已编译/导出一次 zImage、DTB、模块，make 退出 0。
- 当前产物仍不能部署：官方 setlocalversion 强制原厂 release，实验后缀
  未生效。已新增导出拒绝检查。详情和旧产物哈希见 headless-smoke.md。
- 当前下一步：独立实验版本标识补丁和重建；随后模块检查、DTB/SMP 校验及日志通路。
- 不再把取得完整 EPDC 源码当作启动链研究的前置条件，但完整 Android 的显示目标保留。

- 最新路线决策见 route-decision.md：原厂内核缺 namespace/seccomp/LSM，
  只补普通模块无法达到约定目标。已排除该候选，继续新内核路线。
- EPDC 已核实 8 组直接接口，旧版同名函数存在签名差异且缺少 v2 电压入口；
  尚未完成兼容实现，不能直接移植旧文件。下一步先恢复波形/电压数据契约。

- WSL 安装完成：Readom-KOA2 已在 E 盘导入 Ubuntu 22.04.5。
- Linux 8 CPU / 10GB RAM / 8GB swap；依赖安装完成，可直接联网。
- Linux Git 副本 /home/builder/readom-work/readom；源码 linux-stock。
- ARM GCC 4.9 可执行，归档来源及独立校验值见 build-environment.md。
- 完整上游主机测试：49 passed、0 failed、24 skipped，日志
  /home/builder/artifacts/host-tests.log。Linux KOA2 EpdcAbi.h 语法检查通过。
- C 已开始，build-stock-kernel.sh 可重复执行；host dtc 的 yylloc 问题
  以 make 命令行 HOSTCFLAGS=-O2 -fcommon 解决，仅影响主机工具。
- C 当前真实阻塞：mxc_epdc_v2_fb.c:792 包含 mxc_epdc_fb_lab126.c，
  官方内核归档和已解压源码里未找到该文件；编译退出 1，不能宣称内核通过。
- 没有关闭 EPDC、修改原厂驱动、加载模块或进行真机交接。

### 当前下一步

1. 审计官方同机型其他固件归档及公开源码镜像，寻找缺失的 Lab126 EPDC
   包含文件，核实来源、许可证和与 v2 调用点的兼容性。
2. 若能取得完整源文件，以单独补丁和来源记录补齐；若不能，先列出
   所有缺失接口和重实现范围，不能拿空 stub、关闭 LAB126 或换原厂二进制冒充源码复现。
3. 原厂构建通过后才进行模块审计和干净重建，然后推进 D。
4. 产物日志位于 /home/builder/artifacts/stock-build-console.log 和 stock-kernel/。

---

以下为首次安装前的历史记录，不代表当前阻塞。

## 目标及起点

执行 ROADMAP.md 的完整自用目标。分支 port/koa2，起点 6a74732。
官方内核归档校验信息在 source-lock.json；原始采集位于电脑
F:\Kindle\koa2-evidence，第二次采集在 v2 子目录。

## 已完成

- A：路线与持续交接记录已建立；硬件与源码审计见现有文档。
- B 前置检查：Windows 11 Pro 26100，约 16GB RAM、16 逻辑处理器。
- WSL --status 和 --version 均报告尚未安装 WSL；当前 PowerShell 非管理员。
- CPU 的 VirtualizationFirmwareEnabled=True；CIM 的 SLAT 返回 False，
  尚不能据此断定不可用，需要安装后实际验证 WSL2 启动。
- 准备 install-wsl-admin.ps1，拒绝非管理员运行；只安装 WSL 基础组件，
  不自动重启、不安装默认 Ubuntu、不注销任何发行版。

## 当前阻塞及用户操作

需要 Windows 管理员安装 WSL 基础组件。用户以管理员身份运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File F:\Kindle\readom\ports\koa2\install-wsl-admin.ps1
```

ExecutionPolicy Bypass 仅针对此次进程，不改系统持久策略。
按安装输出决定是否重启；保存失败输出以便诊断。

## 下一步（GPT 接手）

1. wsl --version / --status 验证基础安装和 WSL2；若虚拟化失败先定位错误。
2. 读取并保留已有 .wslconfig，再设置 8 CPU、10GB RAM、8GB swap。
3. 从 Ubuntu 官方源下载并校验 22.04 rootfs，导入 E:\Readom-KOA2，
   发行版名 Readom-KOA2，版本 2；在 ext4 内建立 builder 与开发目录。
4. 安装依赖、验证代理和 ARM 工具链；先完成 B 的主机/ARM 冒烟测试。
5. 实施 C 的独立内核构建脚本，源码从 F:\Kindle\koa2-sources 输入。

## 尚未验证

没有 Linux 依赖安装、ARM 构建、内核加载或设备跳转。
当前不能宣称 B/C 完成，也没有可安装的 Android 包。
