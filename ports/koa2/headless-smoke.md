# 替代路线：先验证无显示内核与交接

这是为解除启动链与缺失 EPDC 源码之间的依赖而新增的实验路线。
完整 Android 与微信读书目标不变；显示功能推迟，不以关闭显示冒充完成。

## 构建契约

执行 `bash ports/koa2/build-stock-kernel.sh headless-smoke`。
独立输出目录 out-headless-smoke，产物 headless-smoke-kernel，
内核版本后缀 -koa2-headless-smoke。保持 LAB126 及原厂硬件配置，
仅禁用 FB_MXC_EINK_PANEL 与 FB_MXC_EINK_V2_PANEL。
原 stock 模式继续拒绝缺失源码，未改变原厂基线验收标准。

实验输出不具备显示支持，不是 Android 安装包，不应复制到 Kindle 启动。
构建成功也不代表能交接或安全恢复。

## 当前实际结果

已成功编译并导出一次 zImage（约 6.4MiB）、DTB 和模块，make 返回 0。
zImage SHA256：585d89786cb003d5e5db76ccd68b4eca03a2f89da7f282f4f8b503786139ad03。
DTB SHA256：8df4fe5d732069e1c45d4efa635231578254dd7cf24f85f8bf8cc1c946f144d9。
尚未完成干净重建及模块完整验证。

发现 scripts/setlocalversion 末尾强制 res="-lab126"，导致配置中的实验
后缀被覆盖，实际 release 仍为 4.1.15-lab126。上述旧产物一律不得部署。
构建脚本现增加身份闸门，后续没有实验后缀时拒绝导出，不能把配置字符串
当作实际版本验证。下一步为独立开发内核编写版本标识补丁并重建，保留 stock 模式不变。

## 后续门槛

1. 编译 zImage、DTB、模块，检查具体错误与模块未解析符号。
2. 审计 DTB/内存布局和 SMP 后端，先实现只校验不跳转的组件。
3. 核实无显示情况下可获取的重启日志，建立可观察的实验通路。
4. 以上完成后才准备最小 initramfs 和由用户运行的启动实验。
5. 成功进入实验内核并返回原厂后，再开展 Android 内核接口与显示替代实现。

## 日志初查（源码证据，尚未真机验证）

官方 imx7d-zelda.dts 有 fsl,mxc_ramoops 节点，启用 512KiB record、
256KiB console、256KiB ftrace。节点本身没有给出内存地址，不能照写一个。
kernel/printk/printk.c 另有 CONFIG_LAB126_PRINTK_BUFFER 路径，
经 memblock 验证并预留 LOG_GOAL_LOCATION，提供只读 /proc/printkbuf。
必须继续追踪地址定义、备份时机、冷热重启保留和用户空间读取过程，
目前不能承诺盲启动失败后一定能取回日志。

进一步追踪：printk.c 的 LOG_GOAL_LOCATION=0x9be00000，LOG_GOAL_SIZE=0x40000；
预留两倍大小，后半部分用于保存前次日志，/proc/printkbuf 读这一备份。
这是源码常量，不是已批准的载荷地址；布局验证必须把整段排除在跳转载荷之外。
ramoops 的地址由平台代码动态提供，不能仅从 DTS 节点推定。

SMP 入口初查：arch/arm/mach-imx/platsmp.c 的 imx_smp_ops 在
HOTPLUG_CPU 下提供 imx_cpu_die/imx_cpu_kill；hotplug.c 针对 i.MX7D
调用 imx_gpcv2_set_core1_pdn_pup_by_software。后续应沿原厂 hotplug
机制设计停止次核的前置阶段，而非复用 PW3 的 UP-only 交接假设。
