# 回家后的设备步骤（按顺序）

这里列出所有需要 Kindle 在手边才能做的事，一次连接就能做完。每一步都只读设备或只在
`/mnt/us` 下写报告；没有一步会写分区、改引导或离开原厂系统。

## 1. 准备（电脑上，WSL 里）

```bash
bash ports/koa2/build-stage-test-kit.sh
```

产物在 `~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test/`。脚本会重建暂存模块和
用户态程序，并把 smoke zImage、initramfs 和 SHA256SUMS 放在一起。

## 2. 拷到 Kindle

USB 连上后，在 WSL 里以 root 可写挂载并复制。不要经 Windows 资源管理器复制：公司 DLP
可能会加密 .json/.sh。

```bash
sudo mount -t drvfs F: /mnt/f
cp -r ~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test /mnt/f/extensions/
head -c 16 /mnt/f/extensions/readom-koa2-stage-test/menu.json
```

menu.json 的开头必须是 `{"items"`。如果是 `%TSD-Header`，说明被 DLP 加密了，
KUAL 读不出菜单，需要换别的复制方式。
`.ko`、zImage 和脚本被改动时，设备上的 SHA256SUMS 检查会拒绝运行。
弹出 USB 后再操作 Kindle。

## 3. 运行 Stage Test

KUAL → Readom KOA2 Stage Test → Stage and check handoff (no jump)。

脚本按以下顺序执行：
1. 核对原厂内核身份和套件哈希；
2. 只读复制原厂 .ko；
3. 记录 panic_on_oops、内存和 dmesg；
4. insmod；
5. 暂存 zImage、/sys/firmware/fdt 和 initramfs，然后封存（封存包含改写 FDT、校验布局，
   以及建出跳转阶段要走的复制表并审一遍——没有任何代码会走它）；
6. rmmod，再记录一次。

结果写在 `koa2-stage-test/<时间>/report.txt` 末尾的 `result=`：

| result | 含义 |
|---|---|
| `insmod_refused` | 最可能的结果。模块是对 headless-smoke 树编译的，CRC 与原厂对不上，内核按规则拒绝，什么都没加载。拷回的原厂模块可以用来核对 CRC。 |
| `sealed_plan_valid` | 模块被接受，暂存、FDT 改写、计划校验和复制表审计在设备上全部通过，并已卸载。报告里的 `list_pages`/`list_entries` 是那张表的规模。 |
| `seal_failed` | 模块加载了，但封存被拒。报告里的 seal_stage 说明是哪一步。 |
| `refused_*` | 身份或套件哈希不符，没有加载任何东西。 |

如果设备在 insmod 之后卡住或重启：模块在原厂内核里出错了（oops）。
- 如果 panic_on_oops 为 1，原厂内核约 5 秒后会经原厂重启路径回到原厂系统。
- 原厂在这条路径上会照常把 printk 写到 eMMC 扇区 0x9a400。这是原厂对任何崩溃都会做的事，
  与交接无关。
- 我们没有写任何分区。重启后原厂系统照常启动；报告目录里已经写下的部分仍可用。

## 4. 拷回电脑（WSL 里）

```bash
mkdir -p ~/koa2-evidence/stage-test && cp -r /mnt/f/koa2-stage-test/* ~/koa2-evidence/stage-test/
R=~/koa2-evidence/stage-test/$(cat /mnt/f/koa2-stage-test/LAST_RUN.txt)
python3 ports/koa2/module-crcs.py check --stock $R/stock-modules --symvers ~/readom-work/out-headless-smoke/Module.symvers ~/artifacts/koa2-handoff-module/koa2_handoff_stage.ko
```

原始数据只放在 `~/koa2-evidence/`，不进仓库。

结果分两种情况：
- 第 3 步是 `insmod_refused`：module-crcs 会列出不一致的符号。之后按 README「原厂内核的
  版本检查」处理：用原厂配置重编相关类型，或换掉相应的导入。再用
  `KOA2_STOCK_MODULES=$R/stock-modules` 重建套件，回到第 2 步。
- 第 3 步是 `sealed_plan_valid`：主机侧能验的都验完了。下一个设备步骤是交接执行阶段，
  那需要先写静止顺序里真正碰硬件的 run/undo（quiesce.c 现在只有可测的顺序），并在当次读出
  EPDC 帧缓冲地址加入排除区。跳转蹦床（jump.S）已写好并在 QEMU 的 Cortex-A7 上裸机跑过，
  但没有编进任何模块，也没有在设备上执行过。

## 还需要设备的事（都不在上面这次连接里）

| 事 | 为什么要设备 |
|---|---|
| 标准 sdhci 驱动在这块 eMMC 上的速度模式与 tuning | 只能上机测 |
| EPDC 帧缓冲每次启动的实际地址 | 动态分配，要当次读 |
| 静止顺序每一步对硬件的作用 | quiesce.c 现在只验顺序和回滚 |
| printk 缓冲能否活过 WDOG_B 复位 | 只能上机测 |
| 交接执行（复制并跳转） | 以上都确认之后才谈 |
