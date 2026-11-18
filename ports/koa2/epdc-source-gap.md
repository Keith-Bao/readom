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

下一步继续检查官方更早归档和其他公开源码历史；同时梳理 v2 全部
外部调用及旧实现依赖，只有功能和供电契约明确后才写独立替代实现。
不会使用空函数、关闭 CONFIG_LAB126/EPDC 或强行加载原厂模块来宣称通过。

新增 check-kernel-source.py 在构建开始前检查必要源文件；缺失退出码 2，
输出 JSON。它只证明文件存在与否，补入文件后仍必须重新编译和审计。
