# REQ-MIMI-STABILITY-046 底层接口与内存范围诊断

2026-10-15实施前，接续034/037/041/043/044/045。目标A133 MainBoard V1.2/project4/device4/COM17/固定USB。原rc17/source fe58653135118be74f3ea4da01a787f4ceccbea3/pkg34fc809a45a93661bee74cab2bc1c5093d8d9f103fa8c334d393e80577fbeed8，旧4.9.191系统；HDMI默认off。固定源码/镜像/密钥/绑定不变。

流程：核对MCP scope与所有硬件任务终态、诊断job实际终态→仅读真实meminfo/挂载范围/驱动状态/中断及公开sysfs→与对应SDK源及ABI对照。没有内存或接口证据不能盲写/分配大量内存。根据事实追加单因素对照计划、预置自动恢复并记录原值后，才可临时暂停显示；若只能关图层，必须准确称图层，不当扫描关闭。接口未知、目标不符或任务活跃终止，记blocked/not_run。新诊断helper不得重复执行044同上传/query路径，应先核实可用固定接口。发生失联先有限被动取证待任务终态，再按授权恢复实际旧版本/health/idle/清理；不使用重启恢复冒充修复。

回归不变量：不flash/erase/install、不修改受保护Bootloader/工具或任意寄存器，不更换签名或设备绑定；原候选/失败保留，不将主机发送bytes等同实际接收；UART仅读小文本/控制，私有raw0600，秘密不进公开记录；活跃观察/下载不重启、不重复job；临时变更先记录单变量和自动恢复边界，当前只有只读审查获本批门禁。关键源码/差异/证据备份，禁止重复整个SDK/缓存。

实现范围：本门禁、受限诊断证据output/a133-bottom-diagnosis-20261015；产品源码暂无变更。验收：真实接口和内存基线/源ABI对应/单因素对照/结束恢复分别passed/failed/not_run/blocked；当前均not_run。根因必须有可重复失效路径及修复前后正常功能对照，全部交付条件不缩减。

2026-10-15只读基线与下一步追加：真实MemTotal2033264kB/MemAvailable1887548kB、tmpfs可用1016444kB，非普通缺内存证明；显示LCD1024x768/29fps/runtimeactive/usage1，error15931/irq15951，不先归因。对应SDK DISP宽/高/output常量07/08/09、参数unsigned long[4]与helper一致；GET只读。fb_blank4仅关图层。
原044缺stdout阶段证据，下一步使用原工具源码的有阶段输出小型动态版（不沿用static260KB上传后紧接SSHquery）。源292d92256e696d4b0be8d9d32e6cc214cf6a132c80297e847b9a033a2b3627a8，bin25631cafdf8161d1e3ff6e36ca3028cb343d3bd31fe06975e2492cb894fd4e7f，6800B，原固定Linaro5.3.1，ELF64/AArch64、ld-linux-aarch64.so.1、仅libc.so.6。先实际只读核对板上loader/libc和原空authorized_keys；tmpfs绑定既有维护公钥/0600+600秒自动撤销，固定hostkey网络只上传这6800B且SHA匹配，上传结束后单独先health/idle；再自有UART只执行query并即时记录每次ioctl前后阶段。先分离两阶段，不发blank、不写runtime_enable、不暂停UI。错误无阶段/无法校验/依赖不符立即终止，保持原失败。结束恢复授权、清理自己的tmp文件和关闭串口。此验证仅接口准备，不是失联根因解决或显示隔离通过。私钥仍在源码树外，所有raw私有。

2026-10-15query实测passed：板上loader/libc存在，6800B独立SSH上传后实际SHA一致、健康0；随后独立UART执行GET宽高/output，逐阶段return1024/768/1，runtimeactive/enable0。044上传/query无法分离的失败不删。
下一步临时显示电源单因素对照事前计划：桌面UI、CPU、WiFi、PM与后台保持运行，不STOP UI；仅改变显示runtime gating和固定screen0 DISP_BLANK。先预置30秒自有helper恢复timer，再runtime_enable=1→helper blank→实际runtime_status=suspended/usage0、LCD输出停用及计数停止两次读回，不以黑屏当证明；若计数还增长或blank未成功，不执行网络对照。短周期还原helper restore（厂商首次unblank忽略时至多第二次、active时不重复get_sync），actualactive/usage1/LCD1024x768后runtime_enable=0、真实health0。仅在短周期通过后，另预置420秒自动同接口恢复，以完全相同045的单peer/单文件/512KiB/s/99224064B r11 SWU、原CPU1512000/原服务写/tmp单次HTTP；唯一测试因素是显示电源关闭。下载结束按host终态和板端长度/hash证明，恢复正常显示后health/idle/页面真实状态；有界结果不冒充永久修复。
失败分支：接口未知/暂停无证据立即还原；不取消别人任务；UI可能在显示关闭期间等待vsync，该状态单独记录、不能据此算正常UI通过；helper异常/失联先有界取证与所有job终态核实，再已有授权MCP恢复，前后数据与候选保持。所有timer仅固定自有helper，目录0700/工具700，键binding仍600秒自动撤销；短周期及长周期timer需分别确认终态，不重启破坏活跃观察。恢复失败必须保留显示真实状态，不能直接清空runtime标志宣称恢复。正式正常显示稳定性和系统升级仍单独必验。
