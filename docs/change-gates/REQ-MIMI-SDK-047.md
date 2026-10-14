# REQ-MIMI-SDK-047 设备树面板与内核驱动闭环

2026-10-15事前门禁，接续SDK032/036/038、SYSTEM041/043和STABILITY046。目标A133 MainBoard V1.2、现有B6编译输入及固定1024x768显示，公开入口/独立应用与系统版本保持。

问题与证据：046实际临时LCD runtime暂停后，resume日志cfg_open_flow/close_flow=NULL，active/health0却IRQ368停在25865；之后网络/UART失联，无法把resume调用成功当画面恢复。只读SDK实际sunxi.dtb的/soc/lcd0@01c0c000 used1/driver d320fpc2403/x1024/y768/if4；对应Kconfig驱动存在且默认n，生成.config未启用、实际vmlinux符号不存在。这是候选实际构建输入缺口，失联根因仍未证明。

流程：恢复原rc17并核对实际板上DT面板/内核配置、IRQ持续增长与显示→固定原私有overlay5e947与97组件/8启动保护输入→面板匹配门禁必须在完整构建前及打包前检查，使用实际有效DTB/config以及编译后vmlinux符号，不仅检查源文件存在→缺少/未编译/不匹配立即拒绝并保留原镜像不可发布状态→实际硬件配置与SDK匹配后，在私有repo overlay标准目录修正有效openwrt_linux_defconfig的驱动选择，同步manifest/hash；不更改面板命令/时序/Bootloader或上层业务→记录新的私有提交/配置输入、重建受影响内核及系统产物并更新签名/配对证据，真机LCD恢复/正常显示和原失联路径仍必须重验。没有真机证据不能称修复根因，不把r2原编译passed改写成已支持恢复。

回归不变量：不启用无关面板、不将同分辨率当同面板，不改保护boot0/uboot/scp/bl31/DTB，不替换可信钥/设备绑定，不使用裸flasher或随意register操作；应用独立版本/接口不变，SDK选择只开发工具。保留旧overlay/input/cache和失败历史，单项输入变更应对新hash实际重跑，不能沿用r2验收。私有SDK继续repo overlay入口，新增下载/固件/未追踪源按用户规则处理，排除缓存且不排除openwrt/target。私有raw/凭据不进公开源，记录仅脱敏。

实施范围：公开构建检查器/脚本与需求记录、私有overlay/device/config/chips/a133/configs/b6/linux-4.9/openwrt_linux_defconfig及meta摘要；具体文件在实现提交中列明。当前代码变更未执行，板端匹配与恢复not_run，构建候选驱动缺口failed且已固定事实。验收覆盖真实坏config/DTB/缺symbol拒绝、正确输入及真实内核编译、保护输入无改、SDK/overlay重放与新镜像锁、板端新内核/驱动回调/持续IRQ/显示/传输稳定，逐项绑定passed/failed/not_run/blocked。系统更新回退/真实断电/数据和全功能矩阵仍必需，应用/Release不得提前上传；私有SDK既有受控上传例外保持但未测产品明确not_run。
