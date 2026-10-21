# REQ-MIMI-SYSTEM-040 当前rc17关键数据刷新与恢复依赖

2026-10-14实施前；039实际镜像验签/目标/文件系统/9runtime/归档通过，FEL在同一次专用转换稍后真实online（原timeout保留）；尚未刷写/擦除。回正常启动后先刷新数据备份，不能使用旧rc6/13快照代表当前rc17。

流程：确认4/4/COM17/固定USB、原rc17/trialfalse健康/idle、HDMIoff、原空SSH授权和现存可信包/黄金镜像→仅tmpfs绑定既有维护公钥，原hostkey严格匹配→停止空闲更新服务，核对真实UI/backend PID、start_ticks与固定exe，短时SIGSTOP暂停写入（不向未知/reusedPID发信号）；设置受限恢复计时器→限速接收关键overlay/系统配置/信任登记/SSH主机钥/当前更新器后端与provider、private分区与环境、GPT两端，排除可重建签名releases和临时维护公钥→归档完整读取及逐文件SHA、原始长度/关键文件存在复核，记录私有恢复依赖→只向相同PID/start/exe恢复CONT，重启原服务并实际健康/状态/文件系统读回，撤销tmpfs授权/清理自有临时文件/串口。

备份.delivery/pre-system-flash-20261014-rc17，目录0700/文件0600，archive和私有分区不公开/不进源码，普通门禁只记录摘要/路径。只备份关键材料，不复制SDK/cache/可重建release目录；当前rc17/previous16签名包作为已存恢复依赖绑定摘要。暂停字节快照是崩溃一致备份，不将其当数据库语义/升级后数据验收；稳定性仍未通过。任何停止/传输/恢复/权限/关键文件核对失败阻止刷写，若失联按授权MCP恢复并保留失败。当前backup/复核/恢复not_run，无发布上传。

首轮快照在读取末尾GPT位置时failed：Tina没有blockdev（127），已完成tar/private/env/首GPT但未复核归档；finally核对4个同PID/start/exe并CONT、原更新服务重启，失败和原running标签失真receipt均保留为attempt1。接续前：使用现有/sys/block/mmcblk0/size与queue/logical_block_size实测512字节，核对首GPT当前值与已保存首GPT完全一致后仅补读末GPT和验证现存完整归档，不重跑/覆盖已取得quiesced材料。不是数据库语义通过；若布局变化拒绝复用。


## 2026-10-21 产品启动前数据保护（SYSTEM040 / SYSTEM070）
实施前记录保存在 .delivery/a133-boot-repair-20261021/storage-guard-prerecord.json。
已确认厂商 preinit79 在 ext4 探测失败时直接格式化；原 rc6 首次启动格式化失败证据保留。缺少刷写前文件系统签名，尚不能断言签名丢失的上游原因。
新增产品 preinit78：核对固定 A133 eMMC 的 rootfs_data/p7、UDISK/p9 与成功 ext4 探测；待清除环境变量、未知 sec_storage、错误映射/类型/探测均进入本地恢复循环。退出恢复 shell 必须重新检查，不自动继续格式化。原可信钥、登记和私有分区不改动。空白新板必须经独立受控生产初始化；普通启动不具备擦除授权。
实现：packaging/aitvbox-mimi-update/files/78_mimi_storage_guard、对应 Makefile、tools/update/baseline.py。基线锁与最终镜像读回绑定 hook 字节及0755模式。测试：31项主机存储保护/基线/系统及双产品包 recipe 通过；运行板上只读 preflight 通过，未将此当保护启动验收。hook SHA256 43486d9f9a0ae4faf0d98fc29cc3919c4e54e44dd003e0a5bc15893610ba4fcd。
候选计划：应用1.0.8-rc.30（升级序号30）和私有系统1.0.0-rc.7；旧签名包/标签不覆盖。应用使用已锁定 SDK，系统重建受影响 toolchain/base-files 缓存，保留原字节及 Git mode 来源。最终镜像保护启动、空白板受控初始化、真实回退/断电、完整数据与功能验收仍 not_run；正式发布不通过。
证据：.delivery/a133-boot-repair-20261021/storage-guard-tests.log、实际板端只读预检；后续必须绑定实际新源码/包哈希，不能沿用旧产物结果。


## 2026-10-21 SYSTEM040/SYSTEM070: preinit partition mapping order

Scope: source repair following rc7 r4 first-boot protection stop. The vendor script79 defines link_by_name during script sourcing, but previously only invoked it in its formatter hook, after guard78. The guard therefore saw missing aliases even with initialized data volumes. This fixes alias ordering only; it does not explain the separate FEL filesystem-signature loss.

Prerecord and original guard/test backups were saved before this source change in private delivery evidence storage-mapping-source-r1/prerecord.json. Source base b50dec07a1b56f0cd2f413f89ef2d93a9911330d. No board writes, restarts, builds, signatures, image edits, trust or binding changes in this batch. Public records contain no device credentials.

Flow: source numbered hooks -> invoke guard -> invoke existing vendor mapper -> inspect clear request and exact p7/p9 block mappings/ext4 -> continue to vendor formatter only on success. Mapper missing/failure returns to the existing recovery loop before any probes; recovery exit retries mapping and validation. Alias creation is RAM-only. Existing non-ext4, wrong device, missing volume, pending clear and failed/foreign probe rejection remain enforced. No implicit initialization is added.

Implementation: packaging/aitvbox-mimi-update/files/78_mimi_storage_guard calls link_by_name before data preflight. tests/update/test_storage_guard.py starts with absent mappings and defines the mapper after sourcing guard78, modeling the vendor source/hook order. It also checks mapper failure, absent mapper, repair/retry and all previous negative probes. This replaces the old fixture assumption that by-name aliases already existed.

Verification: original guard with new fixtures failed (two assertion failures and two bounded subprocess timeouts), preserved before-fix-tests.log; fixed guard passed all eight test methods including ten existing probe-failure scenarios. Baseline/product suite ran21, passed20 and skipped one real-program recipe because MIMI_EDITION_PROGRAM_FIXTURES was not provided; affected-tests.log retained without relabeling the skip. No packaging recipe changed in this batch. Host results do not qualify a new image.

The earlier live-repaired rc7 board boot/reboot is historical device evidence, not acceptance of this new source-built candidate. Original rc7 artifact stays immutable. New clean source-built image/physical boot not_run; FEL data retention failed/unresolved, source audit review_required, A/B/rollback/abrupt-power-loss/full-data/voice/Tuya/stability not accepted. Preserve current usable demo while closing these gates.
