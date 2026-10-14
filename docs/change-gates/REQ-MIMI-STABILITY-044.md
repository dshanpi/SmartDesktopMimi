# REQ-MIMI-STABILITY-044 显示扫描与网络写入隔离

2026-10-15实施前，接续034/037/042/043。UI-only暂停观察933.924秒自行重启；CPU1008000已传输失败，不重跑。默认CPU1512000、HDMIoff仍有SSH写tmpfs/持久存储与空闲失联，local32MiB tmpfs写/read和512MiB计算过去通过，仅devnull接收曾通过。不能先归因电源或CPU，也不把克制流量当修复。043原生同guard进入FEL确认failed，但实际进入；没有新安装，需先已授权scope30秒恢复原rc17并核对。

流程：读取所有真实硬件task终态及固定目标、恢复原rc17/健康/idle/原空授权/HDMIoff→只读实际fb/disp blank状态与显示扫描证据，接口未知/不支持则not_run并终止写入→明确自有UI PID/start/exe，临时SSH只既有pubkey/tmpfs+固定hostkey，预置恢复timer→采用034已做的UI暂停基线，仅额外改变fb_blank powerdown，CPU/WiFi/其他后台不变；避免正在写fb时关驱动→读取驱动状态/显示时钟证明扫描确实停止，echo返回0或黑屏不能独立证明→固定旧r11 SWU/hash以512KiB/s写固定/tmp诊断路径，仅读/hash，不解包/安装→同自有UART/网络45min观察，失败保留最后真实阶段与byte/marker。检测结束或错误后只恢复本批同instance UI和原blank，清理授权/暂存；失联先保留短被动取证，再按既有授权恢复并核对旧原始状态。恢复不是修复。自动timer只操作同PID/start/exe以及原blank路径，不杀复用PID。

不变量：未安装/flash/erase、不重置绑定/信任、不改产品源码/版本/SDK输入；原验证候选/失败历史不删。停止显示只是临时诊断，产品必须恢复正常显示后完成功能/稳定性才可交付。observer/传输仍live不得重启或新建重复任务，即使MCP只显示hardwareidle。所有文件/图像走网络，UART只少量文本/控制且原文私有0600；普通证据只白名单阶段/摘要。HDMI持续off，不问电源/拔线前置，不把功耗推断当根因。不运行已有失败限频重复对照。

当前恢复/接口可用性/扫描停用/传输/观察/还原均not_run；若扫描不能证实停止，不能进行“显示DMA关闭”对照或宣称通过。若观察通过，也仅有界诊断证据，需明确可重复修复和正常显示功能矩阵。最终发布禁令保持。
