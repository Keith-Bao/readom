# EPDC 源码缺口调查

## 已验证

- 5.16.2.1.1 官方包内的 Linux 4.1.15：EPDC v2 第 792 行包含
  `mxc_epdc_fb_lab126.c`，此文件不在内核包中。真实编译在此失败。
- 同机型 5.13.6 官方包 `Kindle_src_5.13.6_3732020038.tar.gz` 的
  `gplrelease/linux-4.1.15.tar.gz` 同样没有这个文件；旧包保存在电脑独立文件
  `F:\Kindle\koa2-sources\linux-4.1.15-fw5.13.6.tar.gz`。
- 当前固件输入归档仍保留原名，source-lock 中的校验值不变。
- GitHub 代码搜索确实找到旧版本，尚未找到 4.1 的兼容实现。

## 可研究的公开参考

仓库：https://github.com/katadelos/linux-3.0.35-lab126

固定提交：7ac2585f384573da8cbdcc987a3503e2e6ff1fb4。
文件：drivers/video/mxc/mxc_epdc_fb_lab126.c。
文件声明 Amazon 2012–2017，GPL-2.0-or-later。
仅在外部参考仓库取阅，没有复制到产品或加载到 Kindle。

它不能直接替换 KOA2 文件：使用旧 proc read/write 回调、mach/boardid.h、
MAX77696 EPD 电源接口与 i.MX6 引脚函数。KOA2 为 i.MX7/MAX77796。

## 需要恢复的功能边界

缺失内容涉及波形解码与校验、面板信息、模式映射、温度/电压控制和
proc 调试接口。v2 调用点还涉及 voltage-control/advanced waveform 数据，
不能用旧驱动能解析某个文件就推断整个面板供电与刷新流程兼容。

不会使用空函数、关闭 CONFIG_LAB126/EPDC 或强行加载原厂模块来宣称通过。

## 接口到底有多大（2026-11-21 量出来的）

上面那句"梳理 v2 全部外部调用"一直没做，所以"写独立替代实现"的工作量从来没有数。
量法不靠人眼：给缺失文件放一个空文件，让编译器把缺的符号列出来，然后把空文件删掉
（它留在树里会让 check-kernel-source.py 误判通过）。

缺失文件必须提供的，一共 **9 个函数**：

| 函数 | 做什么 | 值错了会怎样 |
|---|---|---|
| `read_mnt_wfm_v2` | 读出并解压波形文件（2026-11-23 更正：是 `/mnt/wfm` 下的 `.wrf.gz`，不是 eink flash，见下文） | 没波形，刷不了屏 |
| `eink_get_wfm_voltage_control` | 从波形头取电压控制数据 | **面板电压错** |
| `fetch_epdc_vdc_04_data` | 解析波形 0x04 块（VDC） | **面板电压错** |
| `fetch_epdc_adc_06_data` | 解析 0x06 块，填 `reagl_sft`/`reagl_asv` | **REAGL 算法参数错**，直接喂给 PxP WFE-B 硬件 |
| `mxc_set_vcom` | 按 IDME 里的 `idme_vcom_value` 设公共电压 | **VCOM 错**，画面异常，长期可能伤面板 |
| `wfm_name_for_mode` | 波形模式号转名字 | 只影响日志 |
| `paint_string` | 往屏上画字（调试/错误画面） | 只影响调试画面 |
| `mxc_epdc_waveform_proc` | 建 proc 调试入口 | 只少调试接口 |
| `proc_entry_cleanup` | 卸载时清理 proc | 只影响卸载 |

另外它还得 `#include <linux/proc_fs.h>`（`proc_create_data`/`proc_mkdir`/`proc_remove`
三个标准 API 也报缺，是因为缺失文件本来负责引这个头）。

**所以前 5 个不能桩掉**：它们在面板供电和波形算法路径上，错的值有物理后果。
后 4 个只影响日志和调试画面，可以安全地先不实现。这个区分之前没有，
"关掉就行"和"必须做对"混在一起。

（2026-11-23 复核：这 5 个仍然都不能桩，但其中错了会伤硬件的是 3 个，
另外 2 个桩掉坏的是显示。见下面"风险重新评估"。）

它是 `#include` 进第 792 行的，不是链接进来的——同一个编译单元的文本插入。
所以接口是双向隐式的：缺失文件能用 792 行之前的所有静态符号，
792 行之后的 v2 代码又能调它定义的东西。

## 还有第二处缺口（之前没注意）

`include/linux/einkwf.h` **发布了**，它声明了 `einkwf_panel_get_waveform`、
`einkwf_panel_get_vcom`、`einkwf_panel_get_waveform_info` 等 9 个函数——正是取波形和
VCOM 要用的。但**整个内核包里没有任何 .c 实现它们**。

所以缺的不是一个文件，是两处：`mxc_epdc_fb_lab126.c`，加上 `einkwf_panel_*` 这一层。

**2026-11-23 补正：第二处不在重写的关键路径上。** `einkwf_panel_` 这个前缀在整个
内核树的 .c 文件里**一次都没有出现**，只出现在 `include/linux/einkwf.h` 自己
（用 .h 做了正对照，确认 grep 本身是有效的）。2026-11-21 那次空文件编译列出的缺失符号
也只有那 9 个，没有 `einkwf_panel_*`。所以它是一个"发布了声明、包里没有实现、也没有任何
编译进去的代码调用"的头文件——是上游存在一个 lab126 einkwf.c 的证据，
但重写 `mxc_epdc_fb_lab126.c` 不需要先解决它。缺失文件自己会不会调它无从得知，
不过那也只是说那 9 个函数的实现可能更长，不是另一道独立的门槛。

## 反过来，有一件事比原先记的乐观

波形容器格式**不在缺失文件里**：`struct waveform_data_header` 定义在已发布的
`mxc_epdc_v2_fb.c` 第 462 行，字段齐全——checksum、file_length、serial_number、
`mc`（模式数）、`trc`（温度区间数）、`advanced_wfm_flags`、`xwia`/`wmta` 偏移。
在波形文件里导航需要的东西都在。

所以真正未知的只剩一处，而且很具体：**0x04（VDC）和 0x06（ADC）两个子块的内部布局**。
头部告诉你波形数据在哪，但没说这两个 advanced-algorithm 块里面是什么结构。
在整个内核包里搜不到任何描述。

## 0x04/0x06 的布局：设备自己的启动日志里有（2026-11-23）

上一条写"包里搜不到任何描述"，于是下一步定为"从设备读波形文件反推"。
**不用读文件**：已经上传的去标识 dmesg（`materials/2026-11-21/device-evidence/dmesg-before.txt`）
里有缺失文件自己打印的行。先确认这些行确实不是已发布代码打的——
`grep "acd buffer\|vcd buffer\|xswi\|Retrieved voltage" mxc_epdc_v2_fb.c` 没有任何命中，
而 `trt entry` 有（5468、6598 行）。所以这几行只能出自 `mxc_epdc_fb_lab126.c`，
是它内部行为的直接证据：

```
long 70 xswi 373d96
copy (373d96 - 373958) byte @ offset 373958 to acd buffer
copy (373958 - 373098) byte @ offset 373098 to vcd buffer
Retrieved voltage control information (0x8c0)
```

读出来的结构（这台机器，波形文件 `320_R198_AG3902_ED070KC2UA_TC.wrf.gz`，119650 字节）：

| 块 | 偏移区间 | 大小 | 对应 |
|---|---|---|---|
| vcd（0x04，VDC） | 0x373098..0x373958 | 2240 = **0x8c0** | `waveform_vcd_buffer` |
| acd（0x06，ADC） | 0x373958..0x373d96 | 1086 | `waveform_acd_buffer` |

几件事因此确定：

- **`0x8c0` 就是 vcd 块的大小。** 日志里"Retrieved voltage control information (0x8c0)"
  的那个数和 0x373958-0x373098 相等，不是巧合——`eink_get_wfm_voltage_control` 报的是它拷了多少。
- **两个块贴在波形数据的末尾，不在头部。** `xswi`=0x373d96=3620246 是 acd 块的结束。
  `sizeof(struct waveform_data_header)` 实测 48 字节（主机和 ARM 交叉编译一致），
  `wv_data_offs = 48 + trt_entries + 1 = 63`，分配的波形缓冲 3620277 字节，
  所以解压后的文件是 3620340 字节。于是有两种读法，**日志不足以区分**：
  `xswi` 若是缓冲内偏移，两块离缓冲尾 31 字节；若是文件内偏移，离文件尾 94 字节。
  写代码前要先把这一点定下来。
- **已发布代码不负责定位这两块。** `xwia`/`wmta` 在全文件只出现四处
  （490/492 声明、1116/1118 打印），从不用来算地址；两块的定位规则在缺失文件里面，
  日志只告诉我们结果，没告诉我们规则。不要照着这一台机器的数值写死。
- 两块是按连续字节区间整段拷进各自缓冲的（日志里那两行 copy），缓冲由
  `kmalloc`/`kfree` 管理（5515-5520、7868-7871 行）。块里的内容怎么解释是
  `fetch_*` 两个函数的事，日志看不出来。

仍然不知道的是**两块的定位规则和块内的字段排布**：vcd 的 2240 字节怎么按模式和
温区索引，acd 的 1086 字节里 `sft`/`asv` 在哪一位。但这比原来记的"整个布局未知"小：
这一份波形里两块的大小和位置有了，缺的是规则。

## 风险重新评估：5 个里错了会伤硬件的是 3 个（2026-11-23）

原来把 5 个函数都标成"值错了有物理后果"。核对已发布代码和设备日志之后，这个判断要改：

- **`read_mnt_wfm_v2` 不碰硬件。** 设备日志证实它读的是文件系统上的文件：
  `waveform at /mnt/wfm/waveform_to_use/320_...wrf.gz`、`open file ... ok size=119650`，
  不是面板的 eink flash——这正是 `SKIP_READ_EINK_FLASH` 的意思。文件是 gzip 的：
  读进来 119650 字节，解压后约 3.6 MB（分配的波形缓冲 3620277 字节），所以它还得在
  内核里解压。错了就是没波形、刷不了屏，没有物理后果。
- **`mxc_set_vcom` 仍然不能桩，但它容易写对。** 这两件事要分开：值不来自波形文件，
  而是来自 IDME——设备日志 `idme_init: vcom = -1350000` →
  `mxc_set_vcom: vcom_uV=-1350000` → `VCOM = -1346mV` 三行串起来了；而且
  `SKIP_READ_EINK_FLASH` 在 v2 第 91 行**是定义的**，所以 6694 行走的是
  `mxc_set_vcom(fb_data, idme_vcom_value)` 这一支（全文件唯一的调用点，6701 行），
  `mxc_epdc_waveform_init` 整个被编译掉。所以它不需要解析波形，只需要把一个已知整数
  写进 PMIC。但**桩掉它 VCOM 就没人写**：交接之后也许恰好还留着原厂写的值，
  冷启动就是 PMIC 的默认值，不一定是这块面板该有的值。那是实打实的物理后果。
  容易做对，不等于可以不做。
- **`fetch_epdc_adc_06_data` 有原厂自己的默认值可退。** 它填的 `reagl_sft`/`reagl_asv`
  在 `pxp_dma_v3.c` 第 5282-5283 行有 `CONFIG_LAB126` 下的硬编码初值（3 和 21），
  `pxp_probe` 里就设好了。这两个值确实到硬件：`CONFIG_REAGLD_ALGO_CHECK` 在原厂
  配置里**没有**开（只在 pxp_dma_v3.c 3699 行作为 `#ifdef` 出现），所以编进去的是
  `#else` 那支，把 `5 - (asv >> 2)` 和 `5 - sft` 写进 `HW_PXP_WFE_B_SW_DATA_REGS`
  （3705-3708 行）。但那是 PxP 算的 SIT/IAV，属**算法**寄存器，不是面板电源轨，
  所以值错了首先是画面算错。会不会间接影响面板的直流平衡，源码看不出来；原厂自己拿
  3/21 当初值，至少说明这组值在原厂看来可以开机就用。
  它只经 `pxp_wfe_b_process_update` 到硬件，而那个函数全文件只有一个调用点
  （2881 行，在 REAGL/REAGL-D 分支里）。

所以"错了有物理后果"的是 **3 个**：`eink_get_wfm_voltage_control`、
`fetch_epdc_vdc_04_data`、`mxc_set_vcom`。前两个动的是 `v3p3_regulator` 和
`xon_len`/`vpdd_len` 时序（1983-1988 行、5724-5746 行），是夜间模式下真实的供电开关
和 GPIO 时序；第三个动 VCOM。另外两个（`read_mnt_wfm_v2`、`fetch_epdc_adc_06_data`）
桩掉同样会坏显示，但源码里看到的后果都在显示上，不在供电上——这才是和前三个的区别。

**夜间模式是用户空间能触发的，必须在代码里拒绝，不能只靠"别开"。**
`EINK_NIGHTMODE` 在第 94 行是定义的，而 `night_mode` 由 update 请求直接决定
（4228-4233 行：`waveform_mode` 等于 `GCK16` 或 `GLKW16` 就置上，否则清零）。
`fetch_epdc_vdc_04_data` 在夜间分支里的返回值没人检查（1983 行），
失败的话 `xon_len`/`vpdd_len` 就停在上一次的值或者零。
所以第一版要在提交更新的入口直接拒掉这两个波形模式。
顺带一提 `NIGHTMODE_ALGO_SW` 在第 95 行是注释掉的，所以 `GLKW16` 的全屏更新
走的是 REAGL/PxP 那条路（2869-2872 行），不是软件算法那条。

这不改变"完整 Android 上屏仍被挡着"的结论，改变的是**从哪里下手**：
先在提交入口拒掉夜间模式那两个波形、跑通 REAGL 之外的模式，比先解出 0x04 块的
内部布局更容易验证，也更安全。

## 现在的判断

| 问题 | 答案 |
|---|---|
| 工作量有数了吗 | 有：9 个函数，其中 4 个可先桩；5 个不能桩的里面，错了会伤硬件的是 3 个（见上面的复核）。`einkwf_panel_*` 那一层没有任何调用者，不挡重写 |
| 卡在哪 | 0x04/0x06 两块的**定位规则和内部排布**。这一份波形里两块的大小和位置已经从设备日志读出来了（见上），规则还没有 |
| 能先做无屏的事吗 | 能，而且已经在做：headless-smoke 关掉面板就不需要这个文件（`check-kernel-source.py` 对 stock 档位仍然拒绝，对 headless-smoke 放行） |
| 什么时候必须解决 | 只要想让 Android 真正上屏。交接、暂存、跳转都不依赖它 |

下一步不是继续全网找文件（5.13.6 和 5.16.2.1.1 两个官方包都没有，已查）。
**也不再需要"从 eink flash 读波形文件"**：波形是文件系统上的文件
（`/mnt/wfm/waveform_to_use/320_R198_AG3902_ED070KC2UA_TC.wrf.gz`，119650 字节，gzip），
而两块在这一份文件里的大小和位置已经从设备日志拿到了。

按把握排序：

1. **先不解 0x04/0x06，跑非 REAGL、非夜间的模式。** `reagl_sft`/`reagl_asv` 在
   `pxp_probe` 里就有原厂默认值 3/21，而它们只经 2881 行那一个调用点到硬件。
   VCOM 走 IDME 不走波形，但 `mxc_set_vcom` 必须真的写。
   `eink_get_wfm_voltage_control` 和 `fetch_epdc_vdc_04_data` 载入波形时仍会被调用
   （6669-6671 行），已发布代码对它们的失败只打日志或根本不检查，所以第一版可以让它们
   **报错返回、不写任何状态**——这不是放个空函数，而且前提是上面那条：`GCK16`/`GLKW16`
   在提交入口就被拒掉。这条路能先把屏点亮，用来验证 `read_mnt_wfm_v2`（读文件加解压）、
   `mxc_set_vcom` 和 4 个调试函数。
2. **`eink_get_wfm_voltage_control` + `fetch_epdc_vdc_04_data` 这一对最后做**，
   它们动夜间模式的 `v3p3_regulator` 和 xon/vpdd 时序，是真实供电开关。
   它们做对之前，夜间模式那两个波形由代码拒绝，不靠"别开"。
3. 要解块内布局的话，两个大小本身就是约束，但先把维度算清楚：日志打了 14 条
   `trt entry`，而 `trt_entries = wdh.trc + 1`（5461、6594 行），所以 **`trc`=13，
   温区有 14 个**。`mc` 在日志里**没有**，不要猜。
   于是：**vcd 2240 = 14 x 160**，每温区 160 字节，和
   `fetch_epdc_vdc_04_data(fb_data, mode, temp)` 的两个参数相容（160 还能再分，
   比如 5 x 32，但 `mc` 未知之前这只是一种可能）。
   acd 1086 = 2 x 3 x 181，**不被 14 整除**，所以它不是同样的规整二维表——
   181 是质数，1086 在 40 以内只有 1/2/3/6 四个因子，很可能带自己的头部或变长项。
   波形和校准数据属于设备专属，不进仓库（与 PW3 的 `boot/devicedata` 同一规矩）。

新增 check-kernel-source.py 在构建开始前检查必要源文件；缺失退出码 2，
输出 JSON。它只证明文件存在与否，补入文件后仍必须重新编译和审计。

## 2026-11-24 第一版安全替代实现（主机编译通过，未上机）

按上文“先跑非 REAGL、非夜间模式”的安全边界，已在独立目录 `ports/koa2/epdc-v1/`
完成第一版干净实现（`mxc_epdc_fb_lab126.c`、`build-epdc-v1-check.sh`）与主机契约测试
`koa2-epdc-v1`（不修改 `linux-stock`，`koa2-epdc-gap` 仍拒绝原厂树缺文件）：

- `read_mnt_wfm_v2()`：读取 `/mnt/wfm/waveform_to_use` 并用内核 `zlib_inflate` 解压 gzip 波形；
- `mxc_set_vcom()`：校验 VCOM 范围（`-5058900..0 uV`），按 `MAX77696_EPD_VID()` 写入 PMIC；
- 提交入口硬拒绝夜间模式（`WAVEFORM_MODE_GCK16`、`WAVEFORM_MODE_GLKW16` 与
  `GRAYSCALE_8BIT_INVERTED` 转换均返回 `-EOPNOTSUPP`），`eink_get_wfm_voltage_control()` /
  `fetch_epdc_vdc_04_data()` / `fetch_epdc_adc_06_data()` 报错返回且不碰任何供电轨或时序字段；
- 在独立树 `~/readom-work/linux-epdc-v1` 用 ARM GCC 4.9 针对 `imx_v7_zelda_defconfig`
  实际编过 `drivers/video/fbdev/mxc/mxc_epdc_v2_fb.o`（0 错误）。尚未上机点屏。
