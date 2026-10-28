2026-10-28 页面补全第二批：设置小屏按显示声音/外观语言/设备更新分类并支持滚动；用户应用支持刷新与保留选择；蓝牙断开请求具备发送失败、等待、超时及真实状态确认。设置和蓝牙保存失败允许稍后处理并保留未保存状态。216组受影响主机组合、9项契约通过，早期测试失败保留。干净A133完整构建接续；新源码未安装，真实触摸/云端和板子稳定性仍未验收。详见docs/validation/quality076-pages-completion-20261028.json。

2026-10-28 电池接入后卡死追踪：用户确认电池接板载接口。新启动302d0580的142–751秒采样，电池被识别，99%充电转100%满电，4.169–4.181V；用户确认当前桌面和触摸正常。UI/后端回复持续推进，当前不记卡死复现。无信号HDMI每5秒重试已通过正常协议关闭，临时helper删除、串口关闭。源码确认云端重连在后端主循环同步TCP连接，与此前等待栈相符但非历史全部故障因果证明；尚未修改。RTC bootreason=0x2不等于完整复位根因。新驱动/全页候选仍未上板，稳定性未解决。详见docs/validation/quality076-battery-freeze-20261028.json。

2026-10-28 全页构建收尾：源码0f69ea0027d8619ce2128d968eb5faf0a8dd5bbe的干净A133桌面/网页完整构建通过，676源文件，桌面SHA256 b722c0ad87fbd90428a50de42ce1532dfab8925f3aada99a8947bc45d1de8859。808个适用主机组合通过（基础792+修正外部样例16，BT最终40替换对应旧布局结果），9项契约通过，376张预览已整理。原失败记录保留；新产物未签名/安装/上传，不是rc32/rc9重新验收；真实板子卡死/黑屏/异常重启及云端、触摸、升级回退验收仍未完成。详见docs/validation/quality076-all-pages-build-20261028.json。

2026-10-28 全页优化：覆盖首页及11个应用入口，修复视频小屏越界、语音输入/预览布局、系统更新无回复/发送失败反馈、HDMI空记录提示，以及蓝牙小屏控制区不可见。最新BT40组、受影响页面96+32组和9项契约检查通过；全量首轮784/768、复测808/792均保留失败，外部样例/超时复核接续。完整A133构建待干净提交后执行；真实屏幕、触摸、云端和板子稳定性未验收。详见docs/validation/quality076-all-pages-20261028.json。

2026-10-28 用户确认未操作板子：此前启动ID变化记录为非人工重启、原因未明。新采样在同一启动6105–6259秒再次发生SDIO超时，UI activity_ready=false，后端最后回复停在约385秒，后端采样等待网络连接；短暂联网恢复不算稳定通过。系统持有watchdog，但无上次复位状态；无pstore，kernel.panic=3仅说明策略，不证明panic发生。临时降低串口日志至3，未重启/刷机/加载模块，serial-2已关闭。详见docs/validation/quality076-sdio-reboot-followup-20261028.json。

2026-10-28：SDIO发送信用读取失败退避已提交私有硬件源码80cab21并同步SDK。18项主机模拟、35对象完整A133模块及4384文件overlay完整性通过；新模块未上机，SDK缓存尚未重编，不把源码同步视为产品更新。现场先看到旧启动的超时，随后出现新启动（未由助手发出重启，原因待确认）；后续心跳推进、单次网络连通、HDMI协议关闭及临时工具清理通过。原rc8模块保持，物理屏幕/触摸和长期稳定性未验证。详见docs/validation/quality076-sdio-tx-integration-20261028.json。

2026-10-26 用户补充确认：SDIO刷屏当时“桌面卡住或黑屏”，本次真机表现记为 failed_user_confirmed；此前UI心跳不能替代可见桌面/触摸验收。当前COM17在线但被占用，已请求释放；已知SSH端点超时。未抢占串口、重启或刷机。后续先恢复诊断连接，再检查和恢复。详见 docs/validation/quality076-sdio-physical-report-20261026.json。

2026-10-26：涂鸦智家和100ask.net配对页源码97e19cc已提交；136页面变体、8重试变体、9合同检查与两改动文件A133编译通过，未打包/安装。随后用户提交rc8活动SDIO超时：现场TX读取/高频MMC中断、UI心跳仍更新；隔离退避候选18项模拟和A133编译通过，未集成/上机。仅临时降低串口输出级别，原值保留，故障未恢复；串口已关闭。证据docs/validation/quality076-sdio-tx-storm-20261026.json。初始原因、真实屏幕/触摸、新模块与整体稳定性继续未验收。

2026-10-26 IoT/pairing pages improved: device cards/totals and setup/error guidance, inline numeric controls, prominent pairing code and bound completion state, retryable refresh-send failure. 136+8 host renders,9contracts and both A133 changed-TU compiles passed. See docs/validation/quality076-iot-pairing-polish-20261026.json. No new package/device deployment. User now reports active CMD52 RTO log storm; separate live diagnostic evidence work/sdio-storm-20261026, preserve incident and avoid assuming UI changes are installed.

2026-10-25 pairing QR repeated work fixed: identical valid URL skips encoding; initial/refresh failures retry on next status. 16 host LVGL renders, 2 registry tests, 8 byte-identical visual comparisons and A133 changed-TU compile passed. Details docs/validation/quality076-assistant-pairing-qr-cache-20261025.json. This follows responsive modal source03c011c and remains uninstalled/unpackaged. Mode/restart concurrency and physical release gates remain open; no board operation this turn.

2026-10-25 assistant pairing modal: 800x480 overflow reproduced and fixed, fixed visible exit with independently scrolling copy; 56 host LVGL assistant renders, pointer/state checks, two registry tests and changed-TU A133 compile passed. See docs/validation/quality076-assistant-pairing-responsive-20261025.json. Source only: rc32/rc9 artifacts unchanged, new source is not installed. Board remains recovered rc8/rc31. Actual touch/cloud/scan, backend mode/restart and full physical release acceptance remain open.

2026-10-25 user explicitly confirmed the physical screen is lit and showing the desktop. Old rc8 runtime and physical LCD recovery now passed_user_confirmed; rc9 was not flashed and remains physically unqualified. No further power, boot-mode or update action; serial validation session closed.

2026-10-25 recovery follow-up after user reports black screen: fresh normal rc8 boot observed, boot ID 7caee1dc-709b-4b3b-8db8-cd9fb5227a02, uptime advanced30/50/125seconds, UI activity ready and heartbeat advancing, WiFi DHCP successful. LCD driver reports unblank1024x768/backlight51/fps29; physical screen has not been observed or user-confirmed. No new power/reboot/flash issued in this follow-up; do not invent causality for the new boot. HDMI again disabled with normal protocol, absent process confirmed. Exact stale task authorization symlink and temporary helper removed, serial verification passed. Prior failure remains historical; current normal runtime recovery supersedes earlier unknown boot state. rc9 installation and full physical acceptance still not_run. Evidence: physical-acceptance-20261025/user-recovery-result.json.

# Latest physical acceptance — 2026-10-25

2026-10-25 physical acceptance attempt: bound serial and fresh temporary SSH access confirmed the board still ran system rc8 / factory app rc31. Old-version UI heartbeat and p7/p9 zero ext4 errors were read; these do not qualify rc9. HDMI normal control was set off and monitor/process cessation confirmed. Two byte-identical current logical archives (25 regular files) plus the complete private partition were saved off-device and verified. Eight layout/bootchain/DTB components exactly match the retained rc8 package; rc9 compressed/raw hashes match the signed build evidence.

The fixed serial-shell FEL recipe rebooted and serial reached Boot0 "early jump fel", but no bound USB FEL device appeared. No firmware write was attempted. One authorized bound power recovery returned commanded-on only; no fresh boot serial or SSH recovery followed. Reopening the serial adapter did not produce data. Windows enumeration found no present USB device at the exact saved binding. Last actual board execution state is FEL, normal boot unconfirmed; the cause of absent USB/power response is unresolved. Do not blindly repeat transitions or cycles, and never treat command acknowledgement as physical recovery.

Local temporary SSH private key was removed and serial session closed. The device temporary authorized_keys symlink must be removed after physical access returns; cleanup is not falsely marked complete. Fresh private backups and all failed attempts remain under .delivery/quality-review-20261023/physical-acceptance-20261025 and the local private session backup. Original stale SSH pin/missing authorization and the first two HDMI helper attempts remain recorded; corrected helper used the existing normal protocol, not a product modification.

Status: readiness partially passed on rc8; FEL USB admission/recovery failed; rc9 installation and every new-candidate physical test not_run, blocked on actual USB data/power path. Need check board USB connection and relay-controlled supply/other power feeds, establish normal boot and scoped USB enumeration, then recheck backups/admission and continue. No flash, upload, or stable release. Product build source remains 7a6ec0487e9a2d2ad42cb0494b6e570be41da87e; this is record-only.

---

# Latest continuation — 2026-10-25

2026-10-25 QUALITY076 storage/build continuation: user cleanup restored about21GiB, so the15GiB minimum was met. Standard SDK kernel refresh compiled34driverobjects and reused one unchanged object without capacity-header dependency; all31layoutconsumers rebuilt. Full private system rc9 subsequently completed the normal application/kernel/rootfs/pack/signed-SWU pipeline in383seconds, from public source7a6ec0487e9a2d2ad42cb0494b6e570be41da87e and hardware27d2cf50493e263df79bf61e003043cd040e8f1a. Factory app is signed1.0.8-rc.32, restricted to proposed systemrc9; older system pairings not newly qualified.

Original rc32r1 contract-lock failure and systemrc9r1 bilingual-audit failure are retained. Reviewed all23legacy topicIDs/message layout and4protocol tests before locking optional WiFi topics23/24 (64/128bytes); kept contractv1.2. Explicit existing bilingual selections preserve text/behavior and pass6localization tests plus unchanged shipping audit. Final rc32 clean build/signing used the new source; superseded local candidates remain historical.

Independent final rootfs readback matches all40sealed factory files including updater/desktop and generic empty enrollment. Final WiFi module SHA95727a09de533c6605170e7b235823bec9788c3e08f06a0ba40d9f8b9cb125f2 exactly matches fresh current driver through actual SDK strip recipe with recorded locale; three capacity-fix source hashes match. All build checksums and compressed/raw image hashes pass. New image SHA85db31224ccd55eb23559e6bdcd8e5f881210f753613fd54707abfe4f847baea; SWU bfcb1e8d4294356beff4a2eeb73e17a5a5d82da5af86d0423ff9f9d21012dbb8; app package7c632b8162388b4403b076c81195cad777cc9a0d1353e94bbeff2c50272c4b84. Five user-facing candidate files copied locally and hash matched. No public upload, device operation or stable release. SDK build restores incremental product rootfs to pure state; use archived .delivery/a133-private-system-rc9-r2-20261025 outputs, not a later raw SDK pack.

Evidence .delivery/quality-review-20261023/candidate-refresh-20261025/attempt-r2/result.json and sdk-kernel-refresh-20261025/result.json. Current-board readiness, physical boot/UI/touch/audio/radio/HDMI, true upgrades/rollback/power/data, outer system OTA admission, and source/kernel/compiler closure remain open. Build success is not device acceptance. Next controlled readiness/recovery and actual new-candidate matrix; no redundant build/sign/upload for record-only commits. Entire quality goal remains incomplete.

---

# Latest continuation — 2026-10-24

2026-10-24 QUALITY076 RX capacity promotion and source sync: local hardware commit 27d2cf50493e263df79bf61e003043cd040e8f1a promotes the exact previously compiled three-file candidate. Added durable host tests extract the capacity-protected overlay receive functions, use non-copying IO stubs, and pass 52 ASAN/UBSAN cases. Allocator capacity initialization/reuse are source assertions in this runner; historical allocator runtime evidence remains separate. Overlay integrity passed for all 4384 files. Existing module 0502d0d03180ad3ca3add2a3552b71407e45496bcc1d1afb1868904f3d169ebb and all 35 recorded object hashes were rechecked, without redundant rebuild.

Actual SDK target is .delivery/sdk-restored, established from recorded compiler paths and exact 106-file previous inventory. Initial lookup used the manifest's historical export root and failed before any write; this failed attempt is retained. Three committed files were synchronized with original backups and mode preservation; 103 other tracked driver inputs are unchanged. SDK cached objects/modules remain stale and must not be packaged as this candidate. No board operation, push, signing, or release occurred. Full image build remains blocked by less than 15 GiB free space; CMD52/slab cause, kernel/generated/compiler provenance, physical transfers/stability, upgrade/rollback/power/data gates remain open. Evidence: .delivery/quality-review-20261023/sdio-rx-capacity-promotion/result.json. Next: restore build capacity through reviewed storage management, refresh SDK outputs only when admitted, and complete controlled device readiness/recovery and the remaining 12-domain quality matrix. Overall project remains incomplete.

---

> 2026-10-16T11:04:11.302453+00:00 UI075 rc28包含最后两处展示修正，来源918d2a7；完整构建/签名/三项CI及全部17远端附件复验passed，旧rc27不变。未上机/未稳定发布，物理验收和资源依赖未完成。

> 2026-10-16T10:44:09.701308+00:00 UI075最终细节：组件库说明完整显示、控制中心箭头随开合更新；四页面32图、大屏8组/小屏专用8组指针和A133增量编译passed。大屏脚本用于小屏的失败保留。新源码未打包/未上机，rc27不变；物理及逐像素相同未宣称。

> 2026-10-16T10:33:39.583210+00:00 UI075 rc27受控草稿已同步并逐17附件重新下载验签/SHA/版本复核通过，构建来源0e04298；主机小屏拖动缺口已修复。未上机/未稳定发布，旧附件与main未变。

> 2026-10-16T10:20:12.192505+00:00 UI075窄屏拖动缺口已复现并修复；4组上下跨视口/取消与12组大屏回归、19测试passed。新产品变更需新干净候选，真实触摸未验收。

> 2026-10-16T10:09:27.170992+00:00 UI075交互清单遗漏已补齐：4处来源、21新场景、11应用/59交互源检查passed。候选127611e保持未签名；最终对图、物理屏与小屏跨视口拖动仍未完。

> 2026-10-16T10:05:35.452413+00:00 UI075干净桌面/Web候选构建通过，674编译源与暂存文件逐哈希核对；来源127611e，未签名/未上机。首轮清单及第二轮PATH失败保留，文档不重建。

> 2026-10-16T09:58:42.314390+00:00 UI075干净构建首轮被设置共享源码旧哈希拦截；已完成旧/新设置文件双向兼容证明并更新两冻结哈希，保持原归属和门禁。干净构建待新目录重跑；首次失败保留，未签名/上机。

> 2026-10-16T09:52:47.884570+00:00 UI075批次七：1024/800双语双主题真实指针滑条、静音、HDMI确认/超时及底部通知命中passed；测试/记录增量，未重编设备程序。物理与小屏跨视口组件拖动未验收。

> 2026-10-16T09:38:04.059797+00:00 UI075批次六：温湿度两列/分隔线、编辑铅笔及IoT未配置入口已对齐；20主机图和状态/编辑交互、19回归、A133编译passed，最终视觉/物理验收未完成。计划同步独立开发分支，不改main/旧发布。

> 2026-10-16T09:32:55.373043+00:00 UI075批次五：高光裁切、HDMI点重叠和关闭颜色、小屏控制中心底部不可达已修复。1024/800主机交互及A133编译passed；最终细节对图、完整小屏矩阵和上机仍未完成。见 docs/validation/ui075-visual-polish.json。

> 2026-10-16T09:26:14.108278+00:00 UI075批次四：IoT共享数量/在线摘要、未配置/授权失效/错误/60秒过期及恢复已实现；首页零新增云请求。主机和A133编译通过，真实设备及最终视觉/小屏/上机未验收。见 docs/validation/ui075-iot-summary.json。

> 2026-10-16T09:14:27.063026+00:00 UI075批次三：环境详情/60秒数据过期恢复、系统OTA通知只读投影已实现；28主机图与状态/指针、19回归、A133编译passed。首次异步重建弹层清理失败已修复保留；IoT共享摘要/最终对图/小屏/实机仍未完。见docs/validation/ui075-state-details.json。

> 2026-10-16T08:57:05.206816+00:00 UI075批次二：组件库/编辑栏已对齐一轮，真实指针长按/拖动/拒绝跨尺寸/关闭保留草稿/库打开保存通过；16图、19回归、A133编译passed。环境详情/通知/过期状态/最终对图及实机仍未完，未推送/部署。证据docs/validation/ui075-editor-refinement.json。

> 2026-10-16T08:41:34.162226+00:00 UI075批次一已落实真实LVGL五卡/编辑/库/顶栏/控制中心；19项主机测试、24+12截图交互及A133编译passed。尚未一比一完成：编辑库样式、环境详情/通知/过期状态与拖动及物理屏验证继续；0上机、0镜像/包/Release改动。详见docs/validation/ui075-components-host.json，原始output/ui075-implementation。

# PAIR071 签名候选及远端草稿

记录时间：2026-10-15T14:14:51.573980+00:00。关联REQ-MIMI-PAIR-071、SYSTEM-070、RELEASE-072及用户DELIVERY058授权。

本批三份产物均为受控Draft，需要相应GitHub权限查看。未公开稳定版，不参与设备自动更新；主机通过不能替代真机验收。

| 获取入口 | 远端状态 |
| --- | --- |
| [v1.0.8-rc.26](https://github.com/dshanpi/SmartDesktopMimi/releases/tag/untagged-c6a1ac503dbb1bff62dc) | 17附件全部下载/验签/哈希/版本复验passed |
| [system-a133-v1.0.0-rc.5](https://github.com/dshanpi/SmartDesktopMimi/releases/tag/untagged-6d1f52d26dd96603b6e7) | 21附件全部下载/验签/哈希/版本复验passed |
| [system-a133-v1.0.0-rc.6](https://github.com/dshanpi/SmartDesktopMimi/releases/tag/untagged-4aeafb0d3e1d0cd39b0d) | 21附件全部下载/验签/哈希/版本复验passed |

共享应用1.0.8-rc.26源ccfe883，包SHA256 `63c34a4bc1c51127e43c29563ec7f61f64803769f9eaa4bbe6a64ee61fda524b`。真实A133/Web构建219.1秒、签名准入14项、实际LVGL20变体通过；只声明IPC1.2和本批公开系统rc5/私有系统rc6，不能安装到旧IPC1.1系统。

公开系统1.0.0-rc.5源e65802d，预装app rc25；可通过独立rc26应用升级包更新。私有系统1.0.0-rc.6源ccfe883，预装相同共享app rc26。两版实际完整构建及最终9运行库/loader、压缩/原始映像、SWU签名/内容关联分别passed。版本/源码/哈希不强制对齐，不重标旧包来源。

对外镜像名为 `SmartDesktopMimi-SystemImage-v1.0.0-rc.5.img.gz` 和 `SmartDesktopMimi-SystemImage-v1.0.0-rc.6.img.gz`；内部保留A133 MainBoard V1.2、CPU/ABI、IPC、public/private身份和签名匹配。

SDK来源固定私有100askTeam/A133-AIDesktop-Tina5的fa0d799；保留97组件完整原输入及新增9文件组成的4292文件标准overlay。新组合验证97个HEAD及9个新增SDK实际文件，原4283字节记录不变。新Ubuntu环境的恢复重建尚未执行。私有组件来源SmartDesktopMimi-Private/f80bbfb；只有获授权的二进制进入公共草稿，私有源码、SDK完整输入、单机凭据和签名私钥均不发布。

100ask消费者已区分已绑定/未绑定/未知，使用真实有效期，已绑定页支持手动重查；后台83e7de7已推送，由用户后续合并部署。未部署、未重置账号/设备身份/密钥，真实新绑定和网站解绑仍not_run。

附件release-lock.json固定各自构建源；system-build.json记录SDK/压缩与原始镜像/预装应用；platform-product-lock.json记录公共/私有组成。delivery-snapshot.json及SHA256SUMS覆盖完整受控附件。host-verification.json只记录主机证据。device*.json仅主机匹配夹具，不能作为真实设备登记；system.swu虽有独立签名，但来源系统外层在线OTA前代配对未验收。

旧rc23/rc2及rc24/系统rc3/rc4共81附件和固定标签再次只读核对不变。新59附件完整下载后执行原完整门禁；大文件最多12连接、2MiB段，段状态/范围/长度严格检查，最终整文件SHA及签名核对全部保留。

仍未完成：原A133黑屏failed_unresolved；真实安装、系统升级/回退、断电和数据验收按用户要求deferred/not_run，等待新SDK Ubuntu环境。Ubuntu真实图形登录受控制台条件限制；Tuya云项目/设备、真实语音、账号归属仍未验收。没有用重启恢复或主机数量冒充实机通过，项目整体未完成。

本页为事后证据摘要；此前校验器版本误选、接续脚本变量遗漏、私钥输入框占位误报及顺序下载慢均保留原日志。修复的是本轮发布主机流程，签名产物本体未改，也没有因此重新编译。详细公开证据见 `docs/validation/pair071-delivery-snapshot.json`。

开发主机接续入口 /home/ubuntu/AI-DeskTopBox/output/next-phase-20261013/progress.json。未启动新构建或硬件任务，下一步等待实际新环境/资源后完成保留的物理矩阵。

2026-10-19 LOGIN052：已复现并修复GDM真实X11会话Display为空导致等待。严格从固定GNOME程序/同UID/精确scope/稳定进程身份导出唯一编号，Go启动前独立复核。23 Python及16 Go顶层race测试、当前真实会话发现/错误显示拒绝passed；尚未部署/窗口验收。证据docs/validation/login052-empty-display.json，A133与旧草稿不变。

2026-10-19 LOGIN052：用户确认恢复虚拟机替代原开发机并仅授权Ubuntu本地登记更新；旧登记已单独受限备份，原可信钥不变。修复真实GDM空Display、owner-only 0700 Xauthority与服务UMask目录遍历问题。第三次部署及后续真实X11健康读回passed，supervisor/桌面/Provider均UID1000，原签名rc22载荷保持。前两次部署失败及回退保留；24项Python与16项Go race受影响检查通过。退出/再次登录、完整用户数据、升级后登录与长期稳定仍not_run；A133安装/升级/回退/断电继续deferred。 证据：docs/validation/login052-actual-x11-resume.json。

2026-10-19 SDK025/DEBACTIVATE030：当前Ubuntu干净源码c84c564完整desktop/Web构建通过（672编译源、28暂存文件逐哈希复核，原SDK d92c1a17）。三分辨率窗口及未编译尺寸拒绝、5组实际进程生命周期passed；新增实际SDL指针保存/取消/重启布局消费检查passed。此为未签名原生候选，原真实登录rc22保持；不是升级/完整数据或物理验收。 证据：docs/validation/sdk025-current-ubuntu-20261019.json。

2026-10-19 RELEASE072/LOGIN052：原Ubuntu rc22→rc29签名升级及同一X11会话应用重启通过；设置、信任、登记与后端不变，旧rc22保留。修复发布工具的源接口/运行目标和Ubuntu SDK来源校验，56项相关回归通过。前序失败保留；新包未远端交付，完整数据、登出登录及A133物理验收未完成。详见docs/validation/release072-ubuntu-rc29-20261019.json（公共仓库），原始output/ubuntu-upgrade-20261019。

2026-10-19 DELIVERY058/DEBACTIVATE030：Ubuntu rc29受控草稿20附件完整下载/验签/哈希与deb内嵌载荷核对passed，旧发布/main不变；实际Ubuntu传输包升级/卸载/再暂存通过。GitHub附件名规范化失败已保留，追加映射/远端SHA清单解决；后续命名修复16回归通过，未重打包rc29。来源c84，评审0b73057三CI通过。见docs/delivery/20261019-ubuntu-rc29.md；完整数据、logout/relogin与A133物理未完。

2026-10-19 UI075：布局异常输入丢弃数据已复现并修复，12项真实C检查及原存储2项ASAN/UBSAN通过；完整构建/GUI待续，安装rc29保持。见docs/validation/ui075-layout-input-20261019.json。扩展/媒体Ubuntu消费者未接通，不以文件哈希保留代替功能验收。

2026-10-19 UI075：30ecfc2完整Ubuntu构建及布局保存/取消/损坏拒绝四组实际SDL检查passed，原rc29与设置/信任/登记健康保持。首次npm PATH构建失败保留；详见docs/validation/ui075-layout-input-build-20261019.json。扩展/媒体平台集成与真实logout/relogin继续未验收，不强制注销。

2026-10-19 UBUNTUAPPS076：[扩展应用消费者](docs/change-gates/REQ-MIMI-UBUNTUAPPS-076.md)接入平台目录/socket配置；10原生配置、12实际LVGL变体和禁用端点检查passed。冻结共享代码/源锁不变；签名安装、x86准入、执行/storage及完整数据未完成。见docs/validation/ubuntuapps076-client-20261019.json。


### 完整构建追加结果
干净源码866f7e0fbcc87a45fbec31d263a987be42cbf704使用原Ubuntu SDK锁完成desktop/Web构建，89.24秒退出0；28个暂存文件逐项SHA256通过，编译数据库确认新增客户端读取器实际编入。桌面SHA256 e988a25c20e1e819c270097b4a7c009757e5434e8c3380c58f887d8a63bf33fb。此为未签名、未安装测试候选，未替代rc29；前一阶段not_run记录保留为历史。未来签名必须换新版本重新形成干净源码构建证据。Ubuntu真实扩展安装/运行/storage与媒体完整数据、真实重登录、A133物理验收仍未完成。详见docs/validation/ubuntuapps076-build-20261019.json。


### 2026-10-19 UBUNTUAPPS076平台安装事务
新增独立Ubuntu平台安装器，复用aitapp格式/验签，真实签名声明式安装及升级数据保留、初装/升级4阶段退出恢复与恢复再中断共22项主机检查通过。独立root临时目录，8次真实进程退出；测试用临时发布者钥不进入产品信任。Python3.8测试接口失败已保留。支持无权限schema1前置安装，原生架构准入、sandbox/storage、实际用户数据、平台登录接通与媒体仍未完成。未部署至生产、未修改rc29/冻结SDK/绑定，不重建未变的桌面。见docs/validation/ubuntuapps076-installer-20261019.json。

追加最终结果：拒绝布尔schema后23项通过；在/run独立root目录经受控CLI实际安装已签名测试包，再由UID1000的真实LVGL读取、选择、重开页面通过（800x480截图已看）。临时测试钥和安装目录清理，无生产变动。仅增量重编译UI测试夹具，未重建未变桌面产品。完整原生运行/storage/实际数据目标继续保留。

空data目录准入修复后最终24项安装/恢复回归及受控签名安装→UID1000实际LVGL链路重验通过；原23项和首次复现失败保留。完整原生runtime/storage和生产集成未完。


2026-10-19 UBUNTUAPPS076：独立Ubuntu执行原语32项实际进程测试及父退出链复验passed；execveat绝对路径绕过已复现修复，全部失败日志保留。仅平台前置，签名native准入/storage/broker/profile未完成，安装器继续拒绝native；生产rc29/原信任/身份/A133不变。详见docs/validation/ubuntuapps076-native-runner-20261019.json。


2026-10-19 UBUNTUAPPS076：schema3明确Ubuntu native构建/签名/真实宿主ABI预检及签名包二进制受限执行通过；23新检查+24安装回归+6语言回归passed，失败历史保留。原schema2仍AArch64；安装器尚不开放native，认证storage/broker/profile继续。详见docs/validation/ubuntuapps076-native-admission-20261019.json。

2026-10-20 UBUNTUAPPS076：native提前拒绝修复后的23+24项回归通过，611f2b1三CI通过；当前修复另行远端核对。完整broker/storage/profile未接通，rc29未修改。详见docs/validation/ubuntuapps076-native-admission-20261019.json。

2026-10-20 UBUNTUAPPS076内部存储：22项文件系统/进程与签名升级数据检查passed；无RPC/权限开放/生产部署。下一步认证broker与SDK实际读写。见docs/validation/ubuntuapps076-storage-20261020.json。

2026-10-20 UBUNTUAPPS076内部认证链：22项签名SDK/实际进程/存储检查passed；生产安装与session入口未接通，rc29未变。见docs/validation/ubuntuapps076-broker-20261020.json，原失败保留。

2026-10-20 UBUNTUAPPS076 desktop endpoint: 26 broker/native SDK/session endpoint root tests passed. Active-seat list was a fixture; production Mimi/GDM invocation and service activation not_run. Native installer and rc29 unchanged. See docs/validation/ubuntuapps076-session-endpoint-20261020.json.

> 2026-10-20 UBUNTUAPPS076 session endpoint traceability follow-up: initial a290fa6 gate failed due malformed newly-added batch JSON (literal backslash-n and wrong schema shape). Failure retained in output/ubuntu-broker-076-20261020/traceability-a290fa6.log; batch corrected, root test run 26/26 passed; unprivileged invocation correctly skipped and not counted. Runtime code and production rc29 unchanged. Follow-up CI/readback pending.

> 2026-10-20 UBUNTUAPPS076 desktop endpoint follow-up 9f2b462: traceability, application-update and public-source CI all passed; eight exact-commit files independently read back from GitHub with SHA256 match. Initial a290fa6 traceability failure remains retained. Endpoint remains source-only and production GDM/session/profile, native admission and full data upgrade/rollback remain not_run; rc29 and A133 unchanged. Evidence: docs/validation/ubuntuapps076-session-endpoint-20261020.json.

> 2026-10-20 UBUNTUAPPS076 session policy review: read-only audit showed actual Mimi desktop SID differs from the GDM leader SID; login bridge root session profile and systemd BindsTo/cgroup bind the desktop to session 2. Current endpoint rule would reject it. Prerecorded a cgroup/session-profile validation fix. No IPC call, install, service change, forced logout or rc29 change. Evidence output/ubuntu-broker-076-20261020/session-cgroup-live-audit.json; next code/test work pending.
\n\n2026-10-20 UBUNTUAPPS076：修复desktop endpoint的错误SID相等假设，改为root受控登录profile + active local seat0 X11 + systemd session BindsTo/After + exact cgroup，并保留SO_PEERCRED和二进制inode pin。31/31 root回归通过；只读实机进程审计确认PID 114413在登录服务精确cgroup内。logind的Display属性实际为空，profile固定为:0；规则允许空值，非空必须匹配。r3/r4夹具错误、r6一次瞬时BrokenPipe及修复过程日志保留；最终r8全过。未安装或激活端点/服务、未调用UI IPC、未改rc29或板子；真实native安装、升级/回退数据门禁与A133仍deferred/not_run。见docs/validation/ubuntuapps076-session-cgroup-20261020.json。\n
2026-10-21 SYSTEM070/SYSTEM040/UI075：A133 已由现场修复 rc6 启动签名 rc28，真实健康提交、普通重启及首页/网络/IoT坐标回放通过。首次数据格式化失败保留；六项静态恢复状态一致，Tuya DB变化未验收。新增原始镜像非root权限门禁与基线updater防旧覆盖；41项集合+补跑双产品recipe通过。新系统构建、回退断电/完整功能/稳定性仍未完成。见 docs/validation/a133-boot-repair-20261021.json。

2026-10-21 SYSTEM040：启动前持久分区保护及源绑定基线已实现，31主机检查与板端只读预检passed；保护启动、真实回退断电未验收。准备rc30应用/rc7系统新候选，不覆盖rc28/rc6。见docs/change-gates/REQ-MIMI-SYSTEM-040.md。

2026-10-21 HDMI074/SYSTEM070：标准SDK overlay重放后暴露旧采集来源锁，固定到已提交的HDMI074锁；6负向检查和实际A133编译一致passed。rc7-r1来源未闭合，r2低空间、r3旧锁拒绝均保留；继续新构建。详见HDMI074追加记录。

2026-10-21 SYSTEM040/SYSTEM070 mapping-order source repair: vendor link_by_name now executes before guard probes. New source-order fixtures reproduced original failure, then all8 storage guard tests passed; baseline/product21 ran,20 passed/1 real-program fixture skip recorded. No board mutation/reboot, rebuild, signing or artifact replacement. Evidence .delivery/a133-boot-repair-20261021/storage-mapping-source-r1; docs/change-gates/REQ-MIMI-SYSTEM-040.md. New source-built image and physical acceptance not_run; current live-repaired rc7 demo remains available, data-retention failure/source audit/remaining release matrix unresolved.


## 2026-10-23 rc31 / rc8 candidate preparation (SYSTEM070)

Prepare a new local application 1.0.8-rc.31 and system 1.0.0-rc.8 candidate for the integrated boot and capture/driver fixes. Existing signed rc30 admits only rc6/rc7 and remains unchanged. Fresh source build, a new signed compatibility manifest, sealed baseline and normal system/bootchain checks are required. Original trust, hardware identity, data schema and public/private boundaries are preserved. No application behavior change in this version increment. All new build, physical boot, HDMI stability and upgrade/rollback acceptance remain not_run; no stable release or old-result reassignment. Private batch evidence: a133-boot-repair-20261021/system-rc8-r1.


## 2026-10-23 capture lock integration for rc8

Full candidate r1 stopped before compilation because the public capture entry retained the old source-lock pin. Advance scripts/build_a133_capture.py and tests/sdk_source.py to canonical SDK lock3332510afec829d2fd5643afb8a2af0293e3893155c4d3a275d09c2309d29670, corresponding to the already bounded-runtime-tested no-signal candidate. Add the identical capture check to build_release.sh preflight, before creating output or cleaning application builds. No SDK source copied into public code and no signature/hash/alias checks weakened. Six capture boundary tests passed without skips, actual SDK check and shell syntax passed. r1 failure and its signed application/source remain preserved; revised local rc31/r2 must bind this new commit. Full image and physical acceptance remain not_run. Private evidence: system-rc8-r1/capture-pin-prerecord.json and capture-pin-tests.log.


QUALITY076 edition-v4 integration prepared: added9-file versioned overlay under smartdesktopmimi-a133-provider/edition-v4 and copied only that new directory to restored SDK; edition-v3 before/after hashes identical. Public build_a133_provider selects v4 recipes/source graph/reference hashes, private adapter lock rebound without changing adapter source, check_product_edition reference/version updated. Both ORIGINAL PRODUCT ENTRY full builds succeeded and produced exactly prior independently built candidate hashes (public4cb8d3/privateb3a779), with original source/input checks. Stale v3 selection and private input in public profile rejected. All integration edits currently uncommitted across application(two scripts), private(source-lock), hardware(newedition), need trace records and commit/gate review next. Existing known SDK untracked artifacts retained. No deployment/signing/release; UDP success acknowledgement remains legacy optimistic and pending separate fix. Evidence assistant-provider-integration/{pins,entry-result,negative-checks}.json and both product-provider-result.json.
