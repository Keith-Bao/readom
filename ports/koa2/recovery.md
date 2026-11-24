# 交接失败后怎么回来、怎么知道为什么（源码分析，未在设备上验证）

[ROADMAP.md](ROADMAP.md) 的执行规则里有一条硬门槛：**无法证明交接可恢复时停止真机推进**。
这份文件就是那个证明该有的样子——以及它现在还缺什么。
English: [recovery.en.md](recovery.en.md)。

结论先说：**回得来，大概率也能知道为什么**，但有一种失败模式目前是静默的，
下面第四节给了补救办法。跳转从未执行过，这里全部是源码层面的推断。

## 一、跳转之后会发生什么

跳转之后原厂内核已经不存在了——缓存关掉的那一刻就没有可以返回的东西。
所以"恢复"不是回到原厂内核，而是**让设备重新从 eMMC 启动原厂系统**。

| 情况 | 谁来结束它 | 多久 |
|---|---|---|
| 新内核正常起来 | 不需要结束 | — |
| 新内核在 `imx2_wdt` 探针前卡死，或内核本身停转 | i.MX 看门狗，60 秒 | 最多 60 秒 |
| 新内核过了 `imx2_wdt` 探针、但用户空间未打开 `/dev/watchdog` | 内核定时器继续喂狗，不会自动复位；需长按电源键 | 约 40 秒（人工） |
| `smoke-init` 打开 `/dev/watchdog` 后停喂且不关闭 | i.MX 看门狗按设定超时复位（默认 30 秒） | 最多设定超时 |
| 用户不想等 | 长按电源键约 40 秒 | 立即 |

看门狗的 WDE 位置位后不能清除（[handoff-quiesce.md](handoff-quiesce.md) 第二节），
所以**交接之后看门狗一直在计时**。但要注意 2026-11-19 查明、2026-11-24 复核的一条边界：
实验内核的 `imx2_wdt_probe()` 在 `CONFIG_LAB126` 下会启动内核定时器每 `timeout/2` 秒喂狗，
直到 `/dev/watchdog` 被打开。因此 60 秒自动复位只在**到达 `imx2_wdt` 探针前**、**内核本身停转**，
或 **`smoke-init` 打开 `/dev/watchdog` 并停喂（且不关闭）** 时成立；若内核活着但用户空间没能打开
`/dev/watchdog`，则要靠长按电源键约 40 秒回到原厂。我们不写任何分区、不改引导，且实验内核的
`dump_lastk_to_mmc()` 在关闭 `CONFIG_FALCON_BLK` 后为空操作，不会写 eMMC 原始扇区。

## 二、为什么复位之后日志还在

这一步决定了失败是否可分析。原厂 `imx2_wdt` 带 `fsl,wdog_b`，超时经 WDOG_B 引脚
走到 MAX77796 的 reset 输入。PMIC 把复位分两类（`drivers/mfd/max77696-topsys.c`）：

- **Full Restart**（`FRSTRT`）——各路电源完整重启；
- **Partial Restart**（`PRSTRT`）——部分电轨保留。

复位原因表里 `RSTIN_PRSTRT`（Reset Input Partial Restart）对应的字串是
`reset=watchdog`，也就是说**看门狗经 WDOG_B 触发的是 Partial Restart**，
不是断电级的 Full Restart。Lab126 会把 printk 缓冲放在 DRAM 里跨启动使用，
本身也只有在这类复位保留 DRAM 的前提下才讲得通。

## 三、失败之后去哪里看日志

原厂内核每次启动都做这件事（`kernel/printk/printk.c`，`CONFIG_LAB126_PRINTK_BUFFER`）：

1. `log_goal_first` = 0x9be00000，是本次启动写 printk 的地方；
2. 把这块的**上一次内容**复制到 0x9be40000（后 256 KiB）；
3. 然后清空前半段，避免旧日志反复出现。

`/proc/printkbuf` 读的是第 2 步那份备份，也就是**上一次启动的日志**。
所以失败之后的路径是：

```
新内核把自己的日志写进 0x9be00000 → 死掉 → 看门狗 60 秒复位
→ 原厂内核启动，把 0x9be00000 复制到备份半段 → dd if=/proc/printkbuf 读出新内核的遗言
```

这两段（0x9be00000 + 0x80000）都在交接的持久排除表里
（`koa2_handoff_persistent`），所以复制载荷不会碰它们；新内核自己也开了
`CONFIG_LAB126_PRINTK_BUFFER`，会把同一块预留下来而不是当普通内存用。
ramoops（0x9bf00000+0x100000）同样在排除表里，新内核 `CONFIG_MX6_RAMOOPS=y`
会在同一位置分配（`imx7d_reserve()` 在 `arm_lowmem_limit - 64 MiB` 下分配 1 MiB），
且 `out-headless-smoke/.config` 已开启 `CONFIG_PSTORE_RAM=y` 与 `CONFIG_PSTORE_CONSOLE=y`。
2026-11-24 源码复核确认两处关键细节：
- `arm_memblock_init()` 先调 `printk_reserve_buf()` 再扫描 FDT 保留区，且 `stage.c` 输出空的
  FDT 保留表，因此 `0x9be00000+0x80000` 不会被提前占掉，新内核能正常接管该缓冲区；
- **新发现的早期日志缺口**：实验内核 `kernel/printk/printk.c` 通过 `__va(0x9be00000)` 以普通可缓存
  内存写 `log_goal_first`，`setup_log_buf()` 清零与 `log_store()` 写入都没有刷出 Cortex-A7 D-cache。
  硬件 `WDOG_B` 复位不清理缓存，因此新内核在看门狗复位前刚写入的日志（包括 `KOA2_SMOKE` 结尾行）
  可能留在脏缓存行里丢失；`pstore/ramoops` 虽用 `pgprot_writecombine` 绕过缓存，但要等到
  `mxc_ramoops` 探针后才接管。真机跳转前应为实验内核补上类 PW3 `1043` 的 printk 缓存回写补丁。

## 四、有一种失败是静默的（现在的真实缺口）

如果新内核**在 printk 初始化之前**就死了——还在解压、还在 `head.S`、或者 FDT 没读成——
那么 0x9be00000 里仍然是**原厂上一次的日志**。复位之后原厂读 `/proc/printkbuf`，
看到的是它自己的旧日志，和"什么都没发生"分辨不出来。

这不是理论顾虑：解压和早期启动恰恰是最可能出问题的阶段，而它正好落在这个盲区里。

补救办法很便宜，**现在已经接上了**：交接模块的 `K2S_MARK` 在 printk 缓冲里写一个标记
（`KOA2_HANDOFF v1 attempt=N uptime=Ts entry=0x... fdt=0x...`）。那么复位之后：

| `/proc/printkbuf` 里看到 | 说明走到了哪 |
|---|---|
| 没有标记 | 跳转根本没发生，或者 printk 缓冲没被保留（说明第二节的推断错了） |
| 只有标记 | 跳过去了，但新内核在 printk 初始化之前就死了 |
| 标记后面跟着新内核的日志 | 新内核起来了，死在后面；日志说明死在哪 |

写入已经实现（`koa2_marker_place`，模块侧 `K2S_MARK`，测试 `koa2-marker`、`koa2-stage`、
`koa2-marker-wiring`）。它**不是**跳转，也不打开跳转：不复制载荷、不停设备、不下核，
`execute_available` 仍然是 0。代价是这一次启动的内核日志最后 256 字节。

接的时候有三件事和原来写的不一样，都是实际编译和核对 CRC 时才暴露的：

- **写在活动半区的末尾**，不是开头。下一次启动保存的是活动半区，而开头正是 printk
  自己开始写的地方。写之前先把那 256 字节清零，读的人才知道标记在哪里结束。
- **必须自己把它刷出缓存**。这颗 SoC 的看门狗复位不会清缓存——PW3 那个
  `1043-printk-ring-survives-watchdog-reset.patch` 就是为此存在的——所以留在脏行里的标记，
  在它本要说明的那次复位里会一起消失。`koa2_marker_place` 没有回写函数就拒绝执行：
  写了而出不去，看起来像成功而其实不是。
- **原来记的 `__arm_ioremap` 用不了**。它对 RAM 会 `WARN_ON(pfn_valid(pfn))` 然后返回 NULL，
  而这块是内核预留并映射过的 RAM，`pfn_valid` 成立。所以用的是 `phys_to_virt`，不导入任何符号。

符号的选择也不是随便的，两处踩过：按地址清缓存（`__cpuc_flush_dcache_area`）编译成
`v7_flush_kern_dcache_area`，没有任何原厂模块导入它，CRC 核对不了，原厂加载器会直接拒绝整个模块；
`get_jiffies_64()/HZ` 会引入原厂根本没导出的 `__aeabi_uldivmod`，构建脚本当场拒绝导出。
现在用的是 `flush_cache_all`（编译成 `v7_flush_kern_cache_all`，`mxc_epdc_v2_fb` 导入过）
和 32 位 `jiffies`。整模块 35 个导入全部与原厂 CRC 一致，`passed: true`。
`koa2-marker-wiring` 守着这两个坑不再回来。

标记要在设备上证明的那件事——它真的活过复位——只能分两次做：放一个，重启，再读
`/proc/printkbuf`（它发布的是上一次启动的缓冲）。KUAL 里是
"Marker test: place one" 和 "Marker test: check after restart"
两项（`ports/koa2/stage-test/marker-test.sh`），还没在设备上跑过。

## 五、所以这些条件要同时成立才谈跳转

| 条件 | 现在的状态（2026-11-24 复核） |
|---|---|
| 不写分区、不改引导（且新内核不写原始 MMC 日志） | 已成立（`dump_lastk_to_mmc` 已为空操作） |
| 看门狗兜底与长按电源键回退 | 源码成立，但过了 `imx2_wdt` 探针而未打开 `/dev/watchdog` 时内核定时器会继续喂狗，需人工长按 40 秒 |
| 复位保留 DRAM | 2026-11-23 已在设备证实跨**正常重启**保留标记（`marker_survived`）；**WDOG_B 硬件复位**是否保留仍待跳转阶段验证 |
| printk 缓冲和 ramoops 不被交接覆盖且新内核能接管 | 已成立（持久排除表有测试守着，`printk_reserve_buf()` 与 `imx7d_reserve()` 顺序已核实） |
| 跳转前写标记，使极早期失败可分辨 | 已在设备验证放置并跨正常重启读回（`marker_survived`），`execute_available` 仍为 0 |
| 新内核 printk 缓冲抗看门狗复位丢失 | **未满足**（2026-11-24 审计发现 `log_goal_first` 无 D-cache 回写；真机跳转前需补丁并重建） |
| 静止能走通并且能全部放回 | 2026-11-23 旧空跑失败（EPDC 3 引用、USB 重载报错、触摸需强制重启）；现已拆为只读采集与单项测试推进 |

在 EPDC 引用查明、触摸与 USB 恢复通过、静止空跑可逆且新内核 printk 缓存回写补齐之前，不安排真机跳转。
