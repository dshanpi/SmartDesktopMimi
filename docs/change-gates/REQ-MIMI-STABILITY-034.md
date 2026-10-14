# REQ-MIMI-STABILITY-034 桌面进程单变量隔离

2026-10-14实施前记录，承接023/031失败和033关键路径收敛。当前原rc17已恢复、Wi-Fi powersave on；031仅关闭Wi-Fi powersave仍失联，不能继续把该开关当修复。下一步只改变桌面UI运行状态，区分界面渲染路径与仍运行的内核/后台。

流程：核对project4/device4/COM17/USB及通道空闲，确认原rc17/trialfalse/health/idle、HDMI关闭→记录真实UI PID/路径与Backend/IPKVM PID→设置50分钟一次性恢复timer（调用现有验签start-selected，原版本不变）→仅通过既有platform stop删除mimi-desktop单元并确认原UI PID退出；保留Backend/IPKVM和Wi-Fi原值→45分钟MAC+签名静态页绑定的只读观察，发生失联即失败→恢复UI并重新验签健康检查/原设置复核/清理timer、串口。若设备失联，按已有授权恢复后读回，不能声称timer成功。操作前没有idle/签名/身份条件则blocked，不强行停止。

回归不变量：无应用/系统/驱动安装、CPU频率或网络配置改变、不停止未选定服务或其他硬件任务；桌面暂停会使时钟暂时停住，测试期间不对外宣称UI健康。只有实际UI恢复且健康后才结束。结果仅隔离证据，单次稳定不能算根因修复或发布长稳。SDK032构建在主机独立运行，不干扰。工具/证据output/a133-ui-isolation-20261014；固定rc17源fe58653/包34fc809a…beed8。当前not_run：停止/观察/恢复；版本与发布门禁继续保持。


实际启动记录：rc17/trialfalse、签名健康/idle、原Wi-Fi powersave on、UI/后台PID核对passed；仅停止mimi-desktop，后台/IPKVM保留passed。45分钟只读观察已启动，host PID3775594；板端一次性恢复timer PID3889，调用现有验签start-selected。串口关闭。最终观察/恢复/健康not_run，详见output/a133-ui-isolation-20261014/result-pending.json。运行期间不得把UI暂停视为健康或通过发布稳定性。SDK032仍为实际CMake编译运行，并非已完成。
