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
