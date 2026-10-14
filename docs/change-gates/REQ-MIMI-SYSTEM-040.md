# REQ-MIMI-SYSTEM-040 当前rc17关键数据刷新与恢复依赖

2026-10-14实施前；039实际镜像验签/目标/文件系统/9runtime/归档通过，FEL在同一次专用转换稍后真实online（原timeout保留）；尚未刷写/擦除。回正常启动后先刷新数据备份，不能使用旧rc6/13快照代表当前rc17。

流程：确认4/4/COM17/固定USB、原rc17/trialfalse健康/idle、HDMIoff、原空SSH授权和现存可信包/黄金镜像→仅tmpfs绑定既有维护公钥，原hostkey严格匹配→停止空闲更新服务，核对真实UI/backend PID、start_ticks与固定exe，短时SIGSTOP暂停写入（不向未知/reusedPID发信号）；设置受限恢复计时器→限速接收关键overlay/系统配置/信任登记/SSH主机钥/当前更新器后端与provider、private分区与环境、GPT两端，排除可重建签名releases和临时维护公钥→归档完整读取及逐文件SHA、原始长度/关键文件存在复核，记录私有恢复依赖→只向相同PID/start/exe恢复CONT，重启原服务并实际健康/状态/文件系统读回，撤销tmpfs授权/清理自有临时文件/串口。

备份.delivery/pre-system-flash-20261014-rc17，目录0700/文件0600，archive和私有分区不公开/不进源码，普通门禁只记录摘要/路径。只备份关键材料，不复制SDK/cache/可重建release目录；当前rc17/previous16签名包作为已存恢复依赖绑定摘要。暂停字节快照是崩溃一致备份，不将其当数据库语义/升级后数据验收；稳定性仍未通过。任何停止/传输/恢复/权限/关键文件核对失败阻止刷写，若失联按授权MCP恢复并保留失败。当前backup/复核/恢复not_run，无发布上传。

首轮快照在读取末尾GPT位置时failed：Tina没有blockdev（127），已完成tar/private/env/首GPT但未复核归档；finally核对4个同PID/start/exe并CONT、原更新服务重启，失败和原running标签失真receipt均保留为attempt1。接续前：使用现有/sys/block/mmcblk0/size与queue/logical_block_size实测512字节，核对首GPT当前值与已保存首GPT完全一致后仅补读末GPT和验证现存完整归档，不重跑/覆盖已取得quiesced材料。不是数据库语义通过；若布局变化拒绝复用。
