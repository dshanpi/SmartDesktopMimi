# REQ-MIMI-SYSTEM-043 FES加载器重连取证与可重复安装路径

2026-10-15实施前，接续041任务flash1791995033620116300真实failed/reconnecting8%。当前完整签名镜像55297181/源码640c026/factory13冻结，SDK97和黄金启动链不因诊断改版本；0.2.11源bfa82/build exe a577与installed相同，不修改、替换或旁路共享flasher。原rc17已恢复，GPT/private摘要和16GB容量未变，040关键恢复资料+10KB增量保留，serial25closed。

流程：读取现有任务真实终态/全局scope及串口所有权→只读源码/镜像头/DTB配置/WindowsUSB驱动和RAM工作模式，记录假设与可证伪结果→优先使用现有专用、固定、板级作用域原生流程，在同一个硬件guard保留自己的UARTmonitor，以避免上一轮关闭monitor后看不到FES启动边界；只有目标/签名/备份/idle核对通过且当前正常系统健康，才开始新诊断操作。若选择原生uboot_serial_efex，只允许工具内部既定power-only/打断U-Boot/等待=>/固定efex，不能用任意shell/ADB/devmem进入模式或caller指定入口。

与041方式的差异必须先明确：只有串口可见性与原生guard生命周期变化，镜像与工具不变。新一轮允许原MCP受控安装目标modepartition/verifytrue/postreboot，只有专用模式已证明同绑定online才写，未到阶段not_run；失败保留日志/最后阶段，不能自动反复调用同参数或以增加重连超时藏缺陷。若只捕获RAM加载器，必须使用已有受控能力并保留无介质写入边界，不能用诊断环境变量或裸CLI绕过MCP。

不变量：不停止其他任务、不在任何activeflash时重启/断电/关闭通道，不修改USB绑定/vendorPolicy/可信钥/设备绑定，不替换flasher，不清理原失败或partial。UART原文0600并限大小，普通记录只有脱敏阶段/摘要；文件和图片经网络。诊断与新内核安装/稳定性/系统升级/回退/断电/全数据验收分开；未做不算通过。若发现包/SDK加载器问题，单独预记源码和受影响构建/签名门禁后修复，不能擅自重建受保护Boot0/U-Boot。当前源码分析、取证、模式/安装/GPT/数据/恢复均not_run，无上传。

实际atomic请求（同guard、uboot_serial_efex、serial26 monitor保留、原包/工具）在介质写入前返回ALLWINNER_FEL_NOT_STABLE，没有创建flash task。串口明确当前U-Boot efex→BOOT0→eraly jump fel，同次随后独立扫描固定USB10/1online；不能把false确认当未进入FEL，也不在此误报后重复同请求。header/run/work_mode与当前SDK结构吻合，FES1和黄金Boot0的32项DRAM参数完全相同；未发现头解析错位证据。原SDK旧45bc镜像与新552镜像的FES1/sysconfig/board相同，UBoot/DTB不同；旧UBoot7bfa不等于黄金d461，不能把这个未验证旧包当恢复替代或因差异直接改受保护启动链。历史MCP两条成功task的version同0.2.11，仅提供历史，不推定本轮通过。SDK主机VMware未列出1F3A设备，尚不能证明所有USB归属原因。
原root历史1008000限频已在早期SWU64978944B处失败，因此禁止重复限频实验。下一步独立核查先前未做的显示扫描隔离，与034“仅暂停UI”的失败条件对照；此诊断单独预记需求，镜像、SDK输入和flasher保持冻结，不将显示关闭当交付目标。

2026-10-15结果追加：原生同guard的uboot_serial_efex/preserveSerialMonitor=true请求返回ALLWINNER_FEL_NOT_STABLE，未创建flash task。自有UART实际记录efex/BOOT0/eraly jumpfel，同次之后scope scan在线；仅说明稳定检测未通过，不断言没有进入FEL。043原恢复后实际rc17健康/idle0。最新源LYNX版本0.1.0与已装0.9.5不等价；没有替换shared flasher。证据output/system-fes-trace-20261015，后续系统介质写入及验收not_run。
