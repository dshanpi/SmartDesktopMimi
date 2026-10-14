# REQ-MIMI-STABILITY-037 固定SWU传输时连续串口取证

2026-10-14实施前；接续031/034/035。完整服务45分钟观察无失败，但历史同一SWU写tmpfs可在安装/解包前失联，根因未解决。仅重复同一受限传输并补上全程UART证据；不改CPU/WiFi/驱动/服务，不安装/检查器激活该包。

流程：确认project4/device4/COM17/USB/idle、真实rc17签名健康与原WiFi on/PMauto/HDMI off、tmpfs空间→核对r11 SWU字节99224064/hash da7f53d7fe9785ed1df00953fd96d95a5779772995fe1d936df7493855cb7e33→原授权文件空及固定hostkey确认后仅tmpfs临时bind既有维护公钥→预置清理计时器→独占自有UART被动读取10分钟并同时SSH限制512KiB/s传输到固定/tmp/mimi-trace37/system.swu→实际远端摘要/网络/启动标记取证→完成后关闭自有UART，核对实际版本/健康并撤销临时授权和文件。若三次MCP错误不新开句柄或重放命令，记录阻塞；失联保持取证窗口以获取新启动，随后按既有授权恢复并记录；恢复不算修复。

不变量：不发更新安装/解包/启动链/刷写，不改可信钥/绑定/候选；UART原文0600有上限，普通证据只保留白名单事件、摘要和阶段；不把SSH输入字节数当板上实际文件字节；只处理自有PID/句柄/固定临时目录，原维护授权空文件与WiFi/PM状态最终读回。诊断传输和根因修复/系统升级验收分开，未测not_run。当前preflight/transfer/passive trace/final health/cleanup均not_run；活跃SDK032不重启。
