> 后续PAIR071候选与完整远端状态见[新交接](PAIR071-SNAPSHOTS-20261015.md)。本页原rc24/rc3/rc4保持历史，不覆盖。

# 2026-10-15 受控交付快照

对应 REQ-MIMI-DELIVERY-058、EDITION-062、SYSTEM-070、PAIR-071、RELEASE-072。
本页记录当前实际提交和产物，不代替安装/稳定发布授权。记录时间：2026-10-15T10:29:30.302485+00:00。

## 当前获取入口

三份产物均在同一公共仓库的 **Draft** 中；仓库访问者通常看不到草稿，需要有相应仓库权限的 GitHub 登录。
它们没有公开成稳定 Release，也不参与设备自动更新。链接使用 GitHub 返回的实际草稿地址。

| 内容 | 草稿 | 本次远端状态 |
| --- | --- | --- |
| 共享桌面应用 | [v1.0.8-rc.24](https://github.com/dshanpi/SmartDesktopMimi/releases/tag/untagged-8e1aa2d1b816adbd5fca) | passed：16个附件全部下载复验 |
| 公开版系统 | [system-a133-v1.0.0-rc.3](https://github.com/dshanpi/SmartDesktopMimi/releases/tag/untagged-8d087a4b10568e890cf2) | passed：20个附件全部下载复验 |
| 含私有功能的系统二进制 | [system-a133-v1.0.0-rc.4](https://github.com/dshanpi/SmartDesktopMimi/releases/tag/untagged-193629348e38d1c7a4c1) | passed：20个附件全部下载复验 |

文件中 `validation.json` 的实机项目保持 `not_run`；表中的 passed 只指逐附件下载、大小/SHA、签名、来源与版本关系复验。
普通正式上传/推广门禁仍拒绝不全的必需验收。本次同步沿用用户明确授权的受控快照例外。

## 应用与系统如何配对

- 共享应用为 `1.0.8-rc.24`，源提交 `bedf1a864e2c8c8b0745330349634b0f29f31431`。
- `application.tar.gz` SHA256：`c3339a5299d0f35fd26f724fb6997f956d902087908dc4b39d6cf15d98723375`。
- 它的签名目标只包含本批公开系统 `1.0.0-rc.3`、私有功能系统 `1.0.0-rc.4` 的两个完整运行基线。不能用于旧 rc1/rc2，也不能只按应用版本或 CPU 架构判断兼容。
- 两版系统均预装这份共享应用；两者系统版本独立且全局唯一，避免同一版本映射两种内容。
- 本批系统包装源提交均为 `a6e7a3bb180e8d26c9c8d63ece1a3f417e68421a`；后续发布门禁工具源为 `34bb57b0f7f4e7e2cbac4b115ad5a271102fc41b`。文档或工具后续提交不重标已经构建的二进制来源。

| 系统 | 镜像外部文件名 | 压缩文件 SHA256 |
| --- | --- | --- |
| 公开版系统 | `SmartDesktopMimi-SystemImage-v1.0.0-rc.3.img.gz` | `ea3682b868af338839f4474eb843de30ea77f1e01576becf4c63a16afcf64a7c` |
| 含私有功能的系统二进制 | `SmartDesktopMimi-SystemImage-v1.0.0-rc.4.img.gz` | `7d5648a48574502dccffcec759c3b26c207be874580820e0bab8f3fa2788f7c2` |

镜像外名不带 A133；内部仍保留产品、a133 平台、SoC、MainBoard V1.2、CPU/ABI、系统版本、接口和 public/private 产品版本约束。
`system-build.json` 同时记录解压后原始镜像 SHA，不能把压缩 SHA 与原始 SHA 混用。两份原始镜像均为 556318720 字节。

## 附件的用途与限制

`release-lock.json` 关联原始源码、SDK、产品组件、签名清单与核心附件；`delivery-snapshot.json` 和 `SHA256SUMS` 另外覆盖本次完整受控快照及附加 SWU 材料。
`source.tar.gz` 是对应固定提交的公共源码；私有功能系统中的该附件也只含公共源码，不包含私有适配或 SDK 内容。

`system-manifest.json` / `.sig` 是本批镜像准入清单，签名覆盖实际压缩镜像和声明目标。
`system.swu` 具有独立签名，主机已校验 CPIO、描述签名、A/B 内容哈希与实际 rootfs 的关联；它仍是待验收的离线候选。
没有签发或宣称从旧系统升级到新系统的外层在线 OTA 前代兼容关系，不能据此执行设备系统在线升级。

`device-public.json`、`device-private.json` 和 `system-device-host-*.json` 仅为主机匹配夹具，**不能写入设备冒充真实登记**。
核验继续使用已交接/部署的可信公钥，不能信任附件自己提供的新钥。当前交付不包含烧录、覆盖设备身份、替换可信钥或重置账号绑定操作。

## 三个源码仓库

| 仓库 | 本批固定来源 | 边界 |
| --- | --- | --- |
| [SmartDesktopMimi](https://github.com/dshanpi/SmartDesktopMimi) | 应用 bedf1a8；系统 a6e7a3b；门禁工具 34bb57b | 共享桌面、普通应用、通用 IoT/更新、公共禁用实现及统一 Releases |
| [SmartDesktopMimi-Private](https://github.com/dshanpi/SmartDesktopMimi-Private) | `755636e7bf65495144f29f713eae41e1a5058440` | 涂鸦/100ask 一机一码适配与生产工具；源码保持私有，不复制桌面 |
| [A133-AIDesktop-Tina5](https://github.com/100askTeam/A133-AIDesktop-Tina5) | `cd92e9bbab3dfa8baf506cd0214a833f2745028e` | 板级 SDK 标准 repo overlay；源码保持私有 |

用户已明确允许私有功能的**二进制**放公共仓库草稿；这不扩大到私有源码、SDK 输入、真实单机凭据或签名私钥。
普通应用更新继续共用公开发布，不覆盖私有组件、账号身份和保护数据；已有保留验证属于主机事务范围，实际设备验收仍未执行。

## SDK 恢复与构建接续

SDK 差异入口继续使用私有 SDK 仓库的 `overlay/`、`meta/` 和 `scripts/apply_overlay.sh`，不是另一套组件 patch 目录。
在匹配的 TinaSDK 基线及固定 overlay 提交上使用：

```sh
/path/to/A133-AIDesktop-Tina5/scripts/apply_overlay.sh /path/to/TinaSDK
```

先核对原始 SDK 输入与目标路径，再应用差异；这条命令不代表已取得全部 SDK 或已经重建成功。
公开 `sdk-components.json` 是 97 组件提交/哈希摘要，不是完整 SDK；57 根/忽略输入、原源码恢复包及私有组合材料仍需受控交接。
SDK 摘要 SHA：`90b8172a71a9262a80a0d2aee74249000d7676fe4a5cc45207c19b1d14333812`。
本批已核对 overlay 4283 文件/95 基线及实际完整产品构建输入。详细固定输入见 [SDK 检查点](../update/SDK-INPUT-CHECKPOINT.md)，两版组合方式见 [产品版本](../update/PRODUCT-EDITIONS.md)。
SDK、平台、分辨率和 deb/Tina 包选择只属于开发主机构建工具，不进入桌面界面。T527、RK3576 尚未取得实测支持结论。

## 已通过和仍需完成

本批实际执行了共同应用 A133/Web 编译、两版正式完整系统构建（608.9/617.9 秒）、两版最终 9 运行库/加载器校验、镜像完整解压回读、独立 SWU 签名与 rootfs 关联、两版签名镜像各 1 接受/10 拒绝，以及完整本地草稿门禁。
RELEASE072 的 55 项受影响回归及三个公共源码 CI 已通过；首轮 PATH 和两次镜像构建失败保留，修复后的通过不覆盖旧失败记录。
远端逐份复验元数据见本仓库[56附件复验记录](controlled-snapshots-20261015.json)，含每个附件大小、SHA256及独立校验状态；记录只证明受控草稿下载复验，不代替实机证据。

| 剩余项 | 状态与接续 |
| --- | --- |
| A133 失联根因、真实安装/升级/回退/各阶段断电及数据/功能矩阵 | 用户要求后置；`deferred/not_run`，待新 SDK Ubuntu 环境统一执行 |
| 100ask 配对 | 后台最小补丁已隔离测试并推送；待用户合并部署。桌面权威绑定状态/真实有效期的可选版本化接入仍 `not_run` |
| Ubuntu 真实图形登录及完整数据验收 | 控制台暂不可用，`blocked`；已安装不等于真实登录验收通过 |
| 涂鸦真实设备与语音控制 | 代码框架与部分主机验证已有；真实云项目/设备、音频及账号归属仍 `not_run` |
| 正式稳定发布 | `not_run`；不能用本次草稿同步或主机通过代替全部必需验收 |

100ask 后台补丁在 [fix/mimi-provision-state-20261015](https://github.com/100askTeam/AI-HomePage/tree/fix/mimi-provision-state-20261015)，提交 `83e7de7cb31bb914dc17a0e997f4f67a7b3f5133`。
它只在鉴权后增加明确绑定状态和服务器真实有效期，不解绑、不改密钥或账号归属。本批没有合并、部署、重启线上服务或写真实数据库。
后台单独构建/隔离测试通过；综合网站 CI 仍 failed，实际原因是原前端依赖锁不一致及缺少 `qa/performance` 目录，不将其隐藏为全部 CI 通过。

原 rc23 应用、rc2 系统及既有 25 个附件保持原样。本页不是全项目完成声明；Three.js 目前只有可行性评估，没有改动已批准桌面或本批产物。

## 当前测试板与本批产物的区别

用户报告原测试板先有开机画面、随后黑屏；实际仍为旧应用 rc17，本批 rc24/系统 rc3、rc4 均未装入该板。临时隔离配置已完整撤回，原启动方式、可信钥和设备绑定保留。最小环境中 framebuffer 能成像，实体屏与持续健康未通过；正常启动还有 eMMC/无线失去响应现象。旧内核面板登记缺失线索仅为待实测的显示路径分析，不能把新镜像包含修正写成黑屏已修好。后续按 [STABILITY023](../change-gates/REQ-MIMI-STABILITY-023.md) 和原系统安装门禁继续，不能用本页下载完成代替设备修复。
