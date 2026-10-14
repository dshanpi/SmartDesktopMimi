# REQ-MIMI-SYSTEM-041 已验签新内核系统首次受控安装

2026-10-14实施前；接续039/040。新IMAGEWTY包55297181b5ac35d54efa29c4c546ccab73901b51fbf7a405ebd9094e013401ec/556316672B，SFTP下载本地LYNX project4 cache摘要一致并原生inspect33entries。原生产钥签名/登记A133V1.2目标、ext4、工厂/9运行库/启动链及97源配置闭环passed；冻结系统source640c026/sys1.0.0-rc.1/factory13是首次基线候选，并非最终最新功能发布。

040当前rc17关键snapshot3590667B/76regular已读回复核，private/env/GPT两端长度+CRC/布局通过；4个真实PID/start/exe恢复，health/idle0/trialfalse、原空SSH授权/串口关闭passed，原blockdev缺项失败保留。可信rc17和previous16包、旧黄金链/系统镜像及当前private snapshot可恢复。全数据语义和系统升级验收仍未通过。

流程：再核对绑定4/4/COM17/USB与空闲、当前rc17健康/idle/HDMIoff→专用已验证shell固定FEL recipe（不允许unverified、不用generic shell/ADB/devmem）；一次返回超时先读取同次串口/USB最终状态，不重复发送转换→明确同绑定FELonline才调用lynx_start_flash，firmwarePath仅原SFTP实际cache文件，mode=partition/verify=true/postFlashAction=reboot，允许默认下载清单boot-resource/env/bootA/rootfsA，禁止full_erase/partition_erase/raw全盘覆盖；当前与incoming九分区名字/大小/顺序和实际GPTCRC/offset对照一致，rootfs_data/private/UDISK及B槽没有downloadfile，不对其提供负载或请求擦除→按同taskId读真实终态/verify→正常启动读实际新kernel/应用/信任/数据/健康/HDMI，检查覆盖的系统组件与保留的数据，不以FEL/复制完成当升级通过→失败保存最后真实阶段，不声称未执行分区已写；选择合法恢复路径并保护原数据。

启动链8项与黄金链相同，不重新构建Boot0/U-Boot；只允许同板验证包在固定target写入。只有明确绑定/验签/目标/备份/保留模式满足才写，所有任务仍以原MCP硬件guard执行；不改vendorPolicy，不将full-speed大包标志默认打开。未通过必需整个矩阵不上传。固件首次安装与后续常规版本升级/回退/各阶段断电分开，均绑定实际产物/设备证据。当前flash/verify/boot/data/stability全部not_run。

远端32702反向通道刚断开，同一LYNX进程47332的已配置project4本地8767仍online；只读workbench确认device4/COM17/固定USB/mcpName完全相同后可使用同一MCP本地入口，不改变项目、目标或任何用户配置，未扩大任意网络target。旧serial18已闭合，未破坏活跃任务。

## 实际分区核对纠正（写入前）
原实施前段落“九分区名字/大小/顺序和实际GPT对照一致”是未完成比对时的假设，现明确纠正；不覆盖该历史文本。前八分区名字/start/end一致，UDISK起点一致但结束不同：incoming 15269854，实测30535646；incoming GPT backupLBA15269887对应约8GB模板，实板约16GB。证据 output/system-clean-admission-20261014/incoming-gpt-layout-review.json、current-gpt-partitions.json。差异状态 review_required；尚未下发任何flash/erase写入，设备仍原rc17，串口已关闭。

必须先证明当前原生MCP mode对实际GPT/UDISK的保留或容量适配行为，再选择安装路径；不得以没有UDISK downloadfile推定分区表不变，也不得以3.6MB关键备份覆盖全部UDISK媒体数据。MCP广告command/partition等与已装CLI0.2.11帮助keep_data的对应关系需要真实实现证据，未知时不得猜测。若需要新包调整，重新签名并重跑受影响构建/匹配/布局门禁；原冻结包和失败历史保留。当前完整系统安装、数据保留和稳定性均not_run/failed，禁止上传。

### 分区容量路径只读分析
固定SDK U-Boot组件c77c46bdd5669689ca5ad1452971d6734a2adeb7的sprite/sprite_download.c:558/618/644及938/954显示从实际存储总扇区设置GPT backupLBA/last_usable_lba，末分区结束采用实际last_usable；这解释8GB打包模板与16GB现盘差异可能是运行时容量适配，不直接改包重建。主机算式30535680-34=30535646与实际UDISK结束吻合。需继续核对本次FES加载器配置/实际对应二进制和0.2.11原生行为；源码分析不等于真机保留已通过。
公开OpenixCLI upstream固定aeee88d/0.2.4及100askTeam fork ea0e305/0.1.3显示partition会跳过erase但仍下发MBR和Boot0/Boot1；不能声称partition完全不写GPT/启动链。两源不是当前已装CLI0.2.11的等价证据，不凭名字冒充版本核对。来源/算式/路径及限制见gpt-dynamic-capacity-source-review.json，尚未执行刷写。
