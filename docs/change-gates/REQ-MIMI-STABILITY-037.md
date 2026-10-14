# REQ-MIMI-STABILITY-037 固定SWU传输时连续串口取证

2026-10-14实施前；接续031/034/035。完整服务45分钟观察无失败，但历史同一SWU写tmpfs可在安装/解包前失联，根因未解决。仅重复同一受限传输并补上全程UART证据；不改CPU/WiFi/驱动/服务，不安装/检查器激活该包。

流程：确认project4/device4/COM17/USB/idle、真实rc17签名健康与原WiFi on/PMauto/HDMI off、tmpfs空间→核对r11 SWU字节99224064/hash da7f53d7fe9785ed1df00953fd96d95a5779772995fe1d936df7493855cb7e33→原授权文件空及固定hostkey确认后仅tmpfs临时bind既有维护公钥→预置清理计时器→独占自有UART被动读取10分钟并同时SSH限制512KiB/s传输到固定/tmp/mimi-trace37/system.swu→实际远端摘要/网络/启动标记取证→完成后关闭自有UART，核对实际版本/健康并撤销临时授权和文件。若三次MCP错误不新开句柄或重放命令，记录阻塞；失联保持取证窗口以获取新启动，随后按既有授权恢复并记录；恢复不算修复。

不变量：不发更新安装/解包/启动链/刷写，不改可信钥/绑定/候选；UART原文0600有上限，普通证据只保留白名单事件、摘要和阶段；不把SSH输入字节数当板上实际文件字节；只处理自有PID/句柄/固定临时目录，原维护授权空文件与WiFi/PM状态最终读回。诊断传输和根因修复/系统升级验收分开，未测not_run。当前preflight/transfer/passive trace/final health/cleanup均not_run；活跃SDK032不重启。

取证期间源码只读分析：sunxi_wdt的enabled日志注册后无条件输出，即使BOOTON关闭分支调用stop；不能据该日志断定watchdog正在喂狗或为重启原因。固定恢复SDK配置未启用WATCHDOG_SYSFS，当前实体旧#137配置尚未核对。现有overlay的AXP2202初始输入限制补丁尚未上板，不宣称为根因修复。恢复后增加只读/sys/class/power_supply/axp2202-usb的input_current_limit、voltage_now、voltage_min_design、online，以及可用/proc/config.gz对应watchdog标志；不写电流/电压寄存器、不改变驱动、仍不询问外部供电规格。证据saved-driver-analysis.json。

网络失败取证中增加绑定MCP lynx_adb_list只读USB/ADB可见性检查，确认是否仍有当前USB位置的设备；仅列表，不发ADB shell/重启/刷写/连接任务，也不改变串口独占。单次真实结果单独记录，不把USB在线当系统健康。

增加只读LYNX自动任务清单，排查是否存在后台恢复动作影响此前“本轮未下发重启”的现象；只列表，不触发/取消任何任务，不把任务配置推断为已执行。

实际结果：传输failed（host bytes24666112非实际board bytes），602.74sec/466UART读0字节、无restartmarker，最终两probe无响应。取证无软件安装/解包/重启调用；随后已授权relay30sec恢复。rc17/trialfalse/health0/idle0/UIready、WiFi原on/PMauto、p7p9errors0passed；原空授权/自有tmpfs材料清除及serial15关闭passed。实际旧#137 WATCHDOG_SYSFS/BOOTON未启用，PMIC input_current_limit原始3250、voltage_now原始0不作物理电压/根因结论。整体稳定性/系统升级及永久修复failed或not_run，保留所有旧失败；证据completion-summary及原始private只读日志，不公开。
