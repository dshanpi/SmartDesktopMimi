# REQ-MIMI-SYSTEM-041 已验签新内核系统首次受控安装

2026-10-14实施前；接续039/040。新IMAGEWTY包55297181b5ac35d54efa29c4c546ccab73901b51fbf7a405ebd9094e013401ec/556316672B，SFTP下载本地LYNX project4 cache摘要一致并原生inspect33entries。原生产钥签名/登记A133V1.2目标、ext4、工厂/9运行库/启动链及97源配置闭环passed；冻结系统source640c026/sys1.0.0-rc.1/factory13是首次基线候选，并非最终最新功能发布。

040当前rc17关键snapshot3590667B/76regular已读回复核，private/env/GPT两端长度+CRC/布局通过；4个真实PID/start/exe恢复，health/idle0/trialfalse、原空SSH授权/串口关闭passed，原blockdev缺项失败保留。可信rc17和previous16包、旧黄金链/系统镜像及当前private snapshot可恢复。全数据语义和系统升级验收仍未通过。

流程：再核对绑定4/4/COM17/USB与空闲、当前rc17健康/idle/HDMIoff→专用已验证shell固定FEL recipe（不允许unverified、不用generic shell/ADB/devmem）；一次返回超时先读取同次串口/USB最终状态，不重复发送转换→明确同绑定FELonline才调用lynx_start_flash，firmwarePath仅原SFTP实际cache文件，mode=partition/verify=true/postFlashAction=reboot，允许默认下载清单boot-resource/env/bootA/rootfsA，禁止full_erase/partition_erase/raw全盘覆盖；当前与incoming九分区名字/大小/顺序和实际GPTCRC/offset对照一致，rootfs_data/private/UDISK及B槽没有downloadfile，不对其提供负载或请求擦除→按同taskId读真实终态/verify→正常启动读实际新kernel/应用/信任/数据/健康/HDMI，检查覆盖的系统组件与保留的数据，不以FEL/复制完成当升级通过→失败保存最后真实阶段，不声称未执行分区已写；选择合法恢复路径并保护原数据。

启动链8项与黄金链相同，不重新构建Boot0/U-Boot；只允许同板验证包在固定target写入。只有明确绑定/验签/目标/备份/保留模式满足才写，所有任务仍以原MCP硬件guard执行；不改vendorPolicy，不将full-speed大包标志默认打开。未通过必需整个矩阵不上传。固件首次安装与后续常规版本升级/回退/各阶段断电分开，均绑定实际产物/设备证据。当前flash/verify/boot/data/stability全部not_run。

远端32702反向通道刚断开，同一LYNX进程47332的已配置project4本地8767仍online；只读workbench确认device4/COM17/固定USB/mcpName完全相同后可使用同一MCP本地入口，不改变项目、目标或任何用户配置，未扩大任意网络target。旧serial18已闭合，未破坏活跃任务。
