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
| `read_mnt_wfm_v2` | 从 eink flash 分区读出波形文件 | 没波形，刷不了屏 |
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

它是 `#include` 进第 792 行的，不是链接进来的——同一个编译单元的文本插入。
所以接口是双向隐式的：缺失文件能用 792 行之前的所有静态符号，
792 行之后的 v2 代码又能调它定义的东西。

## 还有第二处缺口（之前没注意）

`include/linux/einkwf.h` **发布了**，它声明了 `einkwf_panel_get_waveform`、
`einkwf_panel_get_vcom`、`einkwf_panel_get_waveform_info` 等 9 个函数——正是取波形和
VCOM 要用的。但**整个内核包里没有任何 .c 实现它们**。

所以缺的不是一个文件，是两处：`mxc_epdc_fb_lab126.c`，加上 `einkwf_panel_*` 这一层。

## 反过来，有一件事比原先记的乐观

波形容器格式**不在缺失文件里**：`struct waveform_data_header` 定义在已发布的
`mxc_epdc_v2_fb.c` 第 462 行，字段齐全——checksum、file_length、serial_number、
`mc`（模式数）、`trc`（温度区间数）、`advanced_wfm_flags`、`xwia`/`wmta` 偏移。
在波形文件里导航需要的东西都在。

所以真正未知的只剩一处，而且很具体：**0x04（VDC）和 0x06（ADC）两个子块的内部布局**。
头部告诉你波形数据在哪，但没说这两个 advanced-algorithm 块里面是什么结构。
在整个内核包里搜不到任何描述。

## 现在的判断

| 问题 | 答案 |
|---|---|
| 工作量有数了吗 | 有：9 个函数 + `einkwf_panel_*` 一层，其中 4 个可先桩 |
| 卡在哪 | 0x04/0x06 子块布局，包里没有，公开 3.0.35 实现是 i.MX6/MAX77696，不能照搬 |
| 能先做无屏的事吗 | 能，而且已经在做：headless-smoke 关掉面板就不需要这个文件（`check-kernel-source.py` 对 stock 档位仍然拒绝，对 headless-smoke 放行） |
| 什么时候必须解决 | 只要想让 Android 真正上屏。交接、暂存、跳转都不依赖它 |

下一步不是继续全网找文件（5.13.6 和 5.16.2.1.1 两个官方包都没有，已查）。
更有把握的两条：一是从设备上真实的波形文件反推 0x04/0x06 布局——头部字段已知，
文件能从 eink flash 读出来，`mc`/`trc` 给出表的维度；二是继续看公开的 4.1 代系实现。
波形和校准数据属于设备专属，不进仓库，现场从设备读（与 PW3 的 `boot/devicedata` 同一规矩）。

新增 check-kernel-source.py 在构建开始前检查必要源文件；缺失退出码 2，
输出 JSON。它只证明文件存在与否，补入文件后仍必须重新编译和审计。
