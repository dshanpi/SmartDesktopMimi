# REQ-MIMI-SYSTEM-042 完整持久数据基线和受控安装恢复边界

实施前2026-10-14，接续041分区容量审查和040关键一致性快照。当前镜像55297181…1401ec主机门禁通过，SDK源码显示实际容量适配，但当前CLI0.2.11不是公开0.2.4源码同版。不能把源码分析当真机GPT保留已通过，也不能把3.6MB关键备份等同整个用户数据。

本批先建立真实持久挂载/数据基线与恢复资料：确认MCP project4/device4/COM17/USB10/1和idle；只读rc17/签名健康/更新idle/HDMIoff与真实挂载、容量、数据目录/存量→固定hostkey和既有维护公钥只在tmpfs临时bind，自动清理计时器，秘密不输出→仅当前实测rootfs_data和UDISK范围，清点用户配置、媒体、应用数据和设备凭据；对非构建缓存内容逐文件hash/mode/owner/link及恢复包，现成可信应用包与系统恢复资料引用而不重复备份；未知/漏项/过大未备份不能标完整→受限SSH传输与完整读回；失败保留原结果并恢复授权/进程/健康，失联按授权relay，重启不等于修复→数据快照就绪后再明确下一批受控写入计划。本批不启动flash/erase/更新安装。

不变量：不改可信钥/绑定/数据，不删除未知文件，不收集秘密到普通日志；原UART0600仅白名单摘要，图片/文件走网络。原4PID仅在实际start/exe吻合和升级idle时暂停，退出恢复；仅处理本批临时目录、授权bind和计时器。源码/build输入不变不重建镜像，不把文档增加改成产品版本。恢复必须包含真实GPT两端/环境/private、当前已验签应用及原平台关键服务；UDISK未写payload也可能布局变，实际恢复能力必须先有证据。尚未跑full-data/restore实机阶段，不标passed；公开上传禁令保留。

当前preflight/数据清点/完整范围备份/读回/cleanup均not_run。后续flash/系统升级回退/断电/全数据和稳定性仍not_run或failed。

实际第一快照failed：1466368B后SSH255、三次网络失败且UART被动0和probe无响应；原partial不当备份，原040完整快照不删。已授权30秒relay恢复；关闭旧19/同scope重新开20后UART正常，真实rc17/trialfalse/busyfalse/health0/idle0/原空授权/HDMIoff读回，RSTATE因JSON无换行使marker识别失败但JSON和尾随RSTATE42:0均真实，需保留该工具判定失败并单独复核。没有flash/erase，也未启动六维修文件下载job。
精简后续快照：只刷新实际用户配置、trust/enrollment及overlay（排除已验签版本目录/临时授权），040平台二进制恢复资料不重复复制；私有/env/GPT当前控制数据继续短读保存。四真实PID/same-instance恢复timer与finally保留，不删除原失败目录；全部读回复核才合格。

第二次small快照在PID identity/readlink阶段SSH255失联，尚未STOP任何进程/未传输快照，原trace/result保留；不能归因服务暂停或大包独有，也不宣称root cause。第二次按已授权30秒relay恢复并重新绑定自有串口；不再反复整包快照或逐命令多SSH连接。
后续仅对26非版本目录文件与原040快照对照发现有变化的tuyadb文件作单连接受限增量：既有维护pubkey临时tmpfs bind+清理timer；一次SSH连接内两次读同一固定文件/gzip，摘要一致才算一致字节捕获，不能当数据库语义验收。其他25文件、platform恢复资料和GPT/env/private沿用已验证原040；六UDISK旧维修文件属于恢复依赖，未取得异地完整副本不得标全恢复覆盖。原失败目录不删，临时授权/自有句柄最终清理；本批依旧不flash/erase。

最后实际结果：26个非应用版本目录持久文件均在040完整快照；早期同盘hash对照25相同，唯一tuyadb已用单SSH两次同字节捕获（1MiB各，摘要d7f46f8a…69920），新增10330B压缩备份读回passed。不把文件一致字节当Tuya云实控/数据库语义验收。
六UDISK旧维修文件恢复copy真实job582551已终止failed：两小文件passed，bootA gzip读出5898240B后SSH255；未开始rootfs大文件，未执行flash/erase。正确CR串口probe本次仍无完成标记+网络失败，随后已授权第三次30秒relay恢复，实际健康待读回。不再启动同一job或覆盖partial目录。
串口诊断修正：冷启动getty阶段只发LF可能无命令行输出，同scope发送单CR得到root prompt；恢复后health/idle/原空授权真实0，旧LF-probe失败与JSON无换行marker失配保留，不把它们推断为硬件死机证据。后续明确CR和输出前换行标记，不静默重放旧输入。
只读新镜像启动脚本未发现mkfs/UDISK格式化调用；仅为已检查/etc/init.d范围，不扩大到完整二进制证明。041对应当前0.2.11与FES动态GPT具体路径尚未完整核对，仍未写入。证据system-data-baseline-20261014/result.json及私有原始材料。

最终第三次恢复完成：真实rc17/hash34fc、trialfalse/busyfalse、health0/idle0、HDMIoff、原authorized_keys空且本批临时文件消失，serial22已关闭。final-recovery.json实际读回passed；失败partial和原040/10KB增量恢复包均保留，未重启同job、未flash/erase、未上传。
