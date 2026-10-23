# REQ-MIMI-HDMI-074 采集链路与系统输入接续

关联 REQ-MIMI-SYSTEM-070 / REQ-MIMI-SDK-069；早期HDMI074主机与私有SDK记录见开发根目录同名需求记录。


## 2026-10-21 系统构建入口补接已提交 HDMI074 组件
完整SDK审计发现当前恢复目录仍有旧启动图/采集源码；标准overlay5ea4118重放并读回4295文件/95仓库通过。rc7-r1虽可编译但其SDK来源未闭合，保留而不资格化。r2因磁盘低于15GiB在编译前拒绝，移除已留哈希的可重建解包副本后r3触发旧采集锁拒绝，失败均保留。
实施前记录 .delivery/a133-boot-repair-20261021/capture-pin-prerecord.json。只更新 scripts/build_a133_capture.py 和 tests/sdk_source.py 的固定采集锁为已提交SDK源591fae6ca411d4372cc2ff8b48576b3e941ea313440bcd2e8f6902efc47afd45；原缺失/变更/符号链接/输入输出哈希门禁不放宽，不回退旧平台源码，不复制平台实现。历史退休源清单保留其当时哈希。
6项源准入/路径/链接器负向回归 passed；实际 A133 编译142输入/28链接输入通过，86144字节、SHA256 3a7e6b220191a569ae1b67642deadac0ce8bb6d1ee1790680cebc7e3b498932e，与固定参考产物一致。HDMI仍默认关闭；真实HDMI延迟未验收。
SDK审计的132个GPU生成链接已逐个证明重定位后仍指向原快照/固定Git提交同字节源码，生成配置仅两个SDK绝对路径变化；原失败审计保留，后续分类记录单列。完整新候选及物理验证继续，factory应用rc30仍为9196735已签名包，本次系统构建入口提交与factory源码分别追溯。

首次 e8865a1 自动追溯检查因新详细记录缺完整需求编号而拒绝；补充本标题后重验，不将该记录错误记为产品运行失败。


## 2026-10-23 capture lock integration for rc8

Full candidate r1 stopped before compilation because the public capture entry retained the old source-lock pin. Advance scripts/build_a133_capture.py and tests/sdk_source.py to canonical SDK lock3332510afec829d2fd5643afb8a2af0293e3893155c4d3a275d09c2309d29670, corresponding to the already bounded-runtime-tested no-signal candidate. Add the identical capture check to build_release.sh preflight, before creating output or cleaning application builds. No SDK source copied into public code and no signature/hash/alias checks weakened. Six capture boundary tests passed without skips, actual SDK check and shell syntax passed. r1 failure and its signed application/source remain preserved; revised local rc31/r2 must bind this new commit. Full image and physical acceptance remain not_run. Private evidence: system-rc8-r1/capture-pin-prerecord.json and capture-pin-tests.log.


## 2026-10-23 audio outcome and causal correction

The user reported HDMI silence with an AMD source computer, despite normal Tuya conversation speaker output. Later the user connected another computer, selected BWW6911 (NVIDIA High Definition Audio), and explicitly confirmed audible sound. This is **passed_user_confirmed** for that source/device combination only; no objective waveform or duration acceptance was performed at the moment of confirmation.

During this audio investigation no A133 firmware, driver, mixer setting or audio configuration was changed by the agent. Earlier observations included no incoming frames, then advancing capture counters with zero-valued PCM. Source inspection verifies the amplitude statistics are taken immediately from captured samples, before resampling and playback. Zero samples do not establish whether silence originated at the source, HDMI receiver or A133 input, and must not be described as proof of an A133 regression.

The AMD screenshots show a BWW endpoint and later a Microsoft generic High Definition Audio driver. This alone does not establish a driver replacement or causal defect. Changing computers also reconnects HDMI and renegotiates capabilities. Driver/output selection/handshake compatibility remains **review_required**, not a proven AMD driver root cause. The earlier categorical implication of an A133 input fault is withdrawn; original evidence remains retained.

## Artifact and evidence binding

System v1.0.0-rc.8, factory application v1.0.8-rc.31; public build source 019a27f050d05e96e37f5b55e1bb6a82c155ef17; SDK 31a5653bea157bcc61e286ceca3d2a20a48e4507. System archive SHA256 abd4e6d12cd050d3aa6231a1d251289fe4239a3c7ea1ab465c10a28597caf88b.

Retained rc7-r4/rc8-r2 images contain byte-identical hdmi_preview and asound.conf. This excludes changes to those two artifacts between the images, not all receiver/kernel behavior. Scoped raw diagnostics remain private in hdmi-audio-incident-20261023-r1; no raw device logs, identities, credentials or desktop screenshots are published. User confirmation is the evidence for actual audible output, not earlier silent samples.

## Remaining gates

No new product build or image is required for this documentation-only result. Existing signed artifacts and version tags stay unchanged. HDMI long-duration stability and the original slab fault, physical animation/touch, upgrade/rollback/power interruption, complete data semantics and source qualification remain open. No stable-release or general source-computer compatibility claim. A future controlled AMD retest should distinguish output selection, actual EDID/handshake and driver identity without assuming the cause.

Detailed public record: docs/delivery/20261023-hdmi-audio-followup.md.
