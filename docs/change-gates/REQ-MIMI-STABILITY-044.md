# REQ-MIMI-STABILITY-044 显示扫描与网络写入隔离

2026-10-15实施前，接续034/037/042/043。UI-only暂停观察933.924秒自行重启；CPU1008000已传输失败，不重跑。默认CPU1512000、HDMIoff仍有SSH写tmpfs/持久存储与空闲失联，local32MiB tmpfs写/read和512MiB计算过去通过，仅devnull接收曾通过。不能先归因电源或CPU，也不把克制流量当修复。043原生同guard进入FEL确认failed，但实际进入；没有新安装，需先已授权scope30秒恢复原rc17并核对。

流程：读取所有真实硬件task终态及固定目标、恢复原rc17/健康/idle/原空授权/HDMIoff→只读实际fb/disp blank状态与显示扫描证据，接口未知/不支持则not_run并终止写入→明确自有UI PID/start/exe，临时SSH只既有pubkey/tmpfs+固定hostkey，预置恢复timer→采用034已做的UI暂停基线，仅额外改变fb_blank powerdown，CPU/WiFi/其他后台不变；避免正在写fb时关驱动→读取驱动状态/显示时钟证明扫描确实停止，echo返回0或黑屏不能独立证明→固定旧r11 SWU/hash以512KiB/s写固定/tmp诊断路径，仅读/hash，不解包/安装→同自有UART/网络45min观察，失败保留最后真实阶段与byte/marker。检测结束或错误后只恢复本批同instance UI和原blank，清理授权/暂存；失联先保留短被动取证，再按既有授权恢复并核对旧原始状态。恢复不是修复。自动timer只操作同PID/start/exe以及原blank路径，不杀复用PID。

不变量：未安装/flash/erase、不重置绑定/信任、不改产品源码/版本/SDK输入；原验证候选/失败历史不删。停止显示只是临时诊断，产品必须恢复正常显示后完成功能/稳定性才可交付。observer/传输仍live不得重启或新建重复任务，即使MCP只显示hardwareidle。所有文件/图像走网络，UART只少量文本/控制且原文私有0600；普通证据只白名单阶段/摘要。HDMI持续off，不问电源/拔线前置，不把功耗推断当根因。不运行已有失败限频重复对照。

当前恢复/接口可用性/扫描停用/传输/观察/还原均not_run；若扫描不能证实停止，不能进行“显示DMA关闭”对照或宣称通过。若观察通过，也仅有界诊断证据，需明确可重复修复和正常显示功能矩阵。最终发布禁令保持。

接口准备实际结果：rc17/health0/idle0恢复；显示class runtime_enable0，平台6000000.disp runtimeactive/usage1，驱动dump err10453/irq10484，仅代表get_status累计非零，未归因。标准fb_blank4只关图层，不停止扫描；因此准备独立固定screen0的临时ARM64静态helper(query/blank/restore)，源fb140228…a9ee3/二进制51bc6d46…9869c/537576B，编译通过并未改产品镜像/SDK。
helper经单SSH gzip260510B上传后板上SHA匹配，但query输出前SSH255/超时；随后网络三次不可达/正确CR UART probe未回应，重新打开自有串口仍空。无法确定是上传残留还是只读query触发，不宣称ioctl根因。没有执行blank/runtime_enable/STOP UI，也未启动observer，显示隔离与扫描停止证明全部not_run。终止此路径，partial/日志保留；scope和持久任务idle后按授权30秒恢复。不能在未证实接口时跳过前置直接blank。恢复健康/授权清理待补。

2026-10-15恢复追加：044故障后恢复原rc17/health0/idle0、SSH授权空、runtime_enable0/原CPU1512000，临时helper消失；没有做blank/STOP UI/observer。045执行前实际核对。只读源码sunxi_fb_blank(FB_BLANK_POWERDOWN)仅set_layer_config(enable=0)，不能当LCD scan停止。044显示隔离仍not_run，query故障原因未知。后续045最终再次核对原状态，见output/a133-http-transfer-20261015/final-recovery.json。
