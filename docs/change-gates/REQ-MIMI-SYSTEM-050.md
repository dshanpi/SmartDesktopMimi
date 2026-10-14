# REQ-MIMI-SYSTEM-050 原生FES失败后的主机USB缓存描述取证

2026-10-15事前，接续SYS048/043。新签名image618/source d7c8572首装实际flash-1792017286420130700已terminal failed于reconnecting8%、media transferred0/未写分区，旧/新镜像同RAM FES握手失败，不能以编译通过或重启恢复冒充新内核已安装。固定Linux project4/device4/COM17/USB pciroot0:pci1400:usb10:usb1；Windows该位置仍PNP OK、serviceusbUDisc/driver1.0.0.1/Problem0。

流程：确认原生任务terminal与scope无活动 → 只查询Windows保存的匹配父hub/port1连接及endpoint描述 → 校验VID1f3a/PIDefe8 → 与保存的SDK源码预期区分 → 保留证据 → 使用既有MCP正常恢复。不得从descriptor推定协议成功；后续修复须有真实根因证据。

回归不变量：硬件操作/boot模式/写入/供电仍只经既有MCP；新脚本只读Windowshub已缓存连接信息，不发送FEL/FES/bulk/控制设备命令、不读写寄存器、不reset端口/hub、不修改驱动/CLI/Bootloader。匹配已保存物理位置及其PnP父hub，调用方不能选其他USB目标；只GET_NODE_CONNECTION_INFORMATION_EX和GET_DESCRIPTOR_FROM_NODE_CONNECTION等已审阅的只读metadata接口，不触发descriptor写入。句柄所有分支关闭，VID/PID/结构不匹配拒绝。WindowsSDK已安装头文件为primary结构依据，保留具体文件版本/哈希；缺少权限/不可用如实failed，不自动提权或更换设备。

位置：work/read_usb_cached_descriptors50.py（操作主机临时诊断脚本，副本只进私有.delivery）、对应输出及本REQ/batch。不进入桌面UI，不改变已冻结candidate；普通证据只字段/摘要，UART及敏感身份保存在私有目录。当前metadata取证/根因/新系统安装/真机均not_run；只有Windowsmetadata不能证明旧FES和新FES描述相同/不同，未测对照必须not_run。
