# Oasis 2 首次实机暂存测试

本地时间：2026-11-21 00:00:50（Asia/Shanghai）；报告时间为 UTC。
测试源码：f858117；结果：sealed_plan_valid。
[设备报告](report.txt) 已检查，仅含内核身份、套件检查和测试状态，不含账户、序列号或书库信息。
[套件哈希](SHA256SUMS) 标识本次实际使用的五个文件；这些二进制后来已按用户要求放入本分支的资料文件夹。

模块加载、封存、卸载成功；execute_available=0。本次未复制载荷到目标地址、未跳转、未启动新内核。
tainted=4097 在测试前后相同，不代表原厂系统无污染。
本报告验证的是这一套模块和这次启动的内存布局，不代表真实交接或 Android 已可用。

## 换电脑继续

1. 拉取 Keith-Bao/readom 的 port/koa2 分支，先读 ../../STATUS.md、../../handoff-quiesce.md 和 ../../device-runbook.md。
2. 不需要重复采集硬件或重新运行暂存测试；先在主机实现静止顺序的硬件 run/undo，并补齐交接当次 EPDC 帧缓冲地址排除。
3. 当前 quiesce.c 只验证执行顺序；jump.S 只在模拟器验证，未接入设备模块。继续保留无跳转的测试入口，不直接改成可执行交接。
4. 主机改动按 tools/run-tests.py --filter koa2 验证。真正交接需先明确恢复路径，再安排用户实机启动。
5. EPDC 源码缺口仍挡着完整 Android；参考 ../../epdc-source-gap.md。

63 个原厂模块、运行时设备数据和完整 dmesg 只保存在家用电脑：
WSL ~/koa2-evidence/stage-test/20261120-160050-26549/，
Windows F:/Kindle/koa2-evidence/stage-test/20261120-160050-26549/。
后续用户明确要求上传：公司电脑现在可从[资料目录](../../materials/2026-11-21/README.md)取得原厂模块与去标识的完整日志。原始未处理日志仍只留本地；运行时 DTB 没有找到独立备份。
