# REQ-MIMI-STABILITY-035 自行重启的被动串口追踪

2026-10-14实施前门禁；承接034界面停止仍网络失联并自行重启、031省电off未解决、023历史失联。原rc17已自行恢复且签名健康0；不是本轮relay恢复。启动日志Watchdog enabled(timeout16sec)和procd watchdog仅是调查线索，不能推定复位原因。

流程：核对固定project4/device4/COM17/USB和通道空闲→确认原rc17/trialfalse/健康idle、HDMI关闭、原Wi-Fi on、UI与后台均在运行→只读取得watchdog0 sysfs(bootstatus/status/state/timeout/timeleft/nowayout)及procd watchdog fd引用、系统watchdog配置→启动45分钟被动UART捕获，同时保留MAC/签名静态页的只读网络观察。捕获不发送命令、不调整频率/驱动/看门狗、不重启、不安装、不打开/dev/watchdog。原始片段仅private0600、限额8MiB；普通状态只保存计数/时间/摘要，敏感消息与凭据不进入公共日志。遇到真实Linux/BOOT0重启标记追加受限脱敏摘要并继续捕获，取得复位前后的证据；发生连接错误重试原handle，确认句柄断开才记录终止，不因读超时重新创建或复位板。

回归不变量：不修改运行候选或业务行为，不把被动捕获的单次稳定视为根因修复/发布长稳。只读检查在观察前完成，观测期间UART只有read；所有调用固定保存作用域。owned handle写入hardware-session.json，由单独捕获进程拥有；主会话/heartbeat不得并发写该句柄。结束或stop标志后关闭该句柄，核对所有权后才更新closed；随后主会话验证实际版本/健康/设置。通道占用则blocked不接管他人。

工具/证据output/a133-reboot-trace-20261014；源fe58653135118be74f3ea4da01a787f4ceccbea3/应用包34fc809a45a93661bee74cab2bc1c5093d8d9f103fa8c334d393e80577fbeed8；目标A133 MainBoard V1.2。当前not_run：初始watchdog读回、被动捕获和最终健康；根因未解决、系统升级矩阵/发布继续未通过。下一步根据真实前后日志定位，证据不足保持unknown，不关闭看门狗掩盖故障。


实际setup blocked：原serial-11打开后pre-state真实rc17/trialfalse读回passed；pre-health逐块发送中MCP连接reset，取消输入和close请求也connection refused。Windows32702确认无监听，LYNX app进程仍在；设备ping在线。没有被动捕获、设置/驱动/候选/relay改动。hardware-session保留open，不能凭失败的close标closed；可能未完成输入不得重放。已请求用户恢复固定A133 MCP通道。接续先核对scope/原handle，读回或取消未完成行，再关闭并复核通道；原句柄明确失效才记终止。output/a133-reboot-trace-20261014/setup-result.json。现有SDK032编译继续，不把MCP阻塞变成整个目标完成或停止。


用户恢复通道后：原serial-11实际connected=true且无输出，固定scope核对后Ctrl-C取消未完成行并close成功。r2新serial-12完成rc17/trialfalse/健康idle/HDMI进程无/Wi-Fi原on检查，passive capture进程3936084已启动45分钟，UART观测仅read；host MAC/static观察同时记录。watchdog sysfs查询实际执行但所有指定属性均无值，procd fd查询也无结果，不把查询退出0当字段已取得；bootstatus/运行timeout仍unknown。禁止并发写serial-12。output/a133-reboot-trace-20261014/r2/capture-status.json，最终结果/健康not_run。


2026-10-14本轮实际结果：MCP恢复后正确scope和idle确认，原serial11残留取消/关闭，r2独占serial12被动45分钟完成并关闭。177次网络/固定静态字节检查无失败，无启动标记。最终serial13读取rc17包34fc…/previous rc16/trialfalse/highwater17、health0/idle0、uptime4653.88/原UI2815 ready、WiFi on/PMauto/p7errors0；自有串口关闭。health结果晚到下一次读取，原harness incomplete记录保留，marker-review绑定实际独立exit0。仅本轮观察passed，034自行重启失败保留，根因未解决；watchdog sysfs无值不记bootstatus通过。
