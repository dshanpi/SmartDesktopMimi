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
