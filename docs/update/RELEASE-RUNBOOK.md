# SmartDesktopMimi 独立应用发布执行门禁

用户确认：2026-10-12（Asia/Shanghai）。执行依据：MIMI-UPDATE-001/1.0.0。
本文件细化既有门禁，不降低原规范要求。当前为开发候选，未完成上线验收。

## 发布边界

- 应用仓库维护统一桌面、业务、资源、应用接口、测试与应用发布工具。
- 平台工程维护 Provider、硬件服务、驱动、内核、系统镜像及更新器/公钥/身份的系统集成。
- 当前 lv_backend 混有业务与硬件功能，必须逐文件登记归属，不能整包迁入应用更新。
- 应用版本独立于系统版本；纯应用发布不要求新硬件标签或系统镜像。
- A133 首先验收；T527、RK3576 各自登记、构建和验收，不继承 A133 通过结果。
- 同一应用版本可发布多个目标包；统一源码不等于统一二进制。索引和每个包独立验签。

## 全流程

| 环节 | 输入及交付物 | 通过条件/失败处理 |
| --- | --- | --- |
| 1. 源码与归属 | 固定应用提交、不可移动标签、应用白名单、平台接口锁 | 来源完整；混入硬件实现或未锁定输入即停止 |
| 2. 构建 | 每目标工具链/sysroot/依赖锁；独立构建日志 | 不构建或替换平台服务；ELF/加载器/依赖与目标一致 |
| 3. 打包签名 | 应用包、目标清单、外部密钥签名 | 全目标组合有效；包白名单、路径、大小、完整性通过 |
| 4. 验收 | 实际包哈希绑定的设备身份、基线和测试证据 | passed/failed/not_run/blocked 明确分开；不得编造物理验收 |
| 5. 多目标组装 | 签名 release-index.json、每目标包/清单、发布锁、源码、SBOM、证据 | 每目标独立门禁；版本/提交/通道/序号一致；目标无歧义 |
| 6. 草稿上传 | 固定远端标签及完整附件 | 不覆盖已发布资产；上传后逐附件下载比对；不合格只留草稿 |
| 7. 公开发布 | 远端重新下载的完整发布目录、外部信任、对应通道验收 | 对远端字节重新执行门禁；stable 所有必需项 passed 才公开 |
| 8. 设备发现 | GitHub Release、签名索引与目标清单 | 排除草稿/错误通道；选择唯一兼容目标；索引有错不回退绕过 |
| 9. 下载与激活 | 完整包、当前真实设备状态 | 验签/哈希/身份/接口/运行库/数据/空间/互斥；停止应用前完成检查 |
| 10. 启动与恢复 | 独立版本目录、持久事务日志、旧版本 | 主循环及后端健康检查；失败回退；断电后恢复完整已验证版本 |
| 11. 留档与复核 | 版本、哈希、目标和结果记录 | 每批更新进度；新包不可沿用旧包的通过证据 |

构建追溯使用准确提交和哈希；安装兼容使用签名目标组合、接口和运行环境范围。
系统发布另锁定硬件/SDK/分区/镜像及预装应用；系统升级和回退均需检查持久应用兼容。
用户数据独立保存；未具备可回退迁移时拒绝数据格式变更。

## 上线前仍需满足

- 独立仓库及来源归属迁移、正式源提交/标签与可分发 SDK 输入。
- A133 真实板型修订登记、系统可信公钥、签名工厂应用和系统基线。
- 真实 GitHub 草稿下载复核、应用升级/回退/断电/数据保留及系统双向配对验收。
- 当前 HDMI 单板维修授权不能作为更新器/系统发布的签名例外。

默认自动检查并提示；自动下载和空闲安装需策略启用。设备不保存 GitHub 管理令牌。

## SDK、镜像与应用的配对门禁

1. 完整硬件 SDK：固定组件提交、配置、本地补丁、未跟踪构建输入与工具链；组件清单不能替代实际源码和补丁。
2. 应用开发 SDK：从已确认系统基线导出工具链、头文件、链接库与接口契约，记录平台、基线和逐文件哈希；只构建桌面。当前应用 SDK 工具仅支持 A133，T527/RK3576 未接入。
3. 系统镜像：由底层工程构建，包含对应板型驱动、Provider、更新器、可信公钥、设备身份机制和指定出厂应用。镜像锁定硬件与 SDK 输入、分区配置、出厂应用及镜像哈希。
4. 应用升级包：仅包含桌面、业务、资源和应用私有依赖；签名声明支持的硬件、系统/接口/运行库范围。需要新平台能力时先发布系统前置版本。
5. SDK 与厂商库公开分发前检查来源与许可；设备凭据、签名私钥及未授权厂商文件不得进入公开产物。

## 本批实现与使用入口

- release_collection.py：逐目标门禁后组装 schema 2 发布锁和签名索引，保留各目标独立的构建锁与验收证据。
- mimi-update sign-index / verify-index：外部信任校验索引、包清单及实际包；同一身份重复出现在多个包时拒绝。
- 在线检查识别 release-index.json / .sig；离线目录使用 install-offline-index --source <directory>，原安装事务重新检查身份和签名。
- publish_draft.py：上传后下载完整资产并重跑门禁；promote_release.py --stage prerelease|stable：重新下载核对后才公开。
- application-publish.yml：统一发布仓库 main 分支手动触发受控 runner，需要预先准备签名候选、外部信任和发布令牌。尚未配置或执行真实远端流水线，不代表源码到签名产物全自动构建已完成。
- 候选目录 /srv/mimi-release/candidates/<tag>，信任 /etc/mimi-release/trust.json；环境 mimi-draft / mimi-prerelease / mimi-stable，发布令牌 MIMI_RELEASE_TOKEN。这些是待部署约定，不是现有设施。
- blocked/failed/not_run 证据只保留为本地候选；按用户最新要求，上传草稿前也必须完成全部必需主机和真机验收。测试夹具不等于真实多平台支持。

本批未执行拆仓、安装系统基线、实际 GitHub 发布或板端升级。门禁记录完成；框架实现仍为候选。

## 已落实的统一入口与来源锁定（2026-10-12）

- 公开源码与所有 Release：dshanpi/SmartDesktopMimi；私有 A133 SDK 补丁：100askTeam/A133-AIDesktop-Tina5（用户授权上传）。应用与系统各自发布，不合并版本号，不公开私有 SDK 内容。
- 应用标签 vX.Y.Z，系统标签 system-a133-vX.Y.Z；其他平台分别使用平台前缀，未验收平台不得发布。旧标签不移动。
- system_repository 与 repository 可以相同；基线生成器写入相同地址。设备先区分标签类型及平台，再执行原有验签、匹配、通道和防降级门禁。
- application-publish.yml 保留入口文件名，但处理应用及系统两种标签；候选目录为 /srv/mimi-release/candidates/<完整标签>。草稿上传、远端复核、公开均调用同一门禁。
- 应用与系统的 source_commit 都是公开仓库内标签指向的发布配方/源码提交；hardware_commit 才是私有 SDK 补丁提交。远端标签检查不能拿私有提交替代公开提交。

### 应用发布必需来源文件

每目标 release-lock.json 除已有字段外，需包含 sdk_lock_sha256，并将 application-build.json、sdk-lock.json、dependency-lock.json 纳入 assets 哈希表。构建记录中的 SDK/契约/平台/源码必须与锁和包清单一致；签名清单中的 resources/application-build.json 及 bin/lvglsim 哈希必须对应实际记录。toolchain_sha256/sysroot_sha256 为 SDK files 对应前缀子集按键排序、紧凑 JSON 编码的 SHA256。dirty=true 仅允许草稿，不可公开。

### 系统发布必需来源文件

系统 release-lock.json 必须记录 platform_id、hardware_repository、hardware_commit、sdk_baseline、sdk_local_inputs_sha256。system-build.json 以 schema_version=1、kind=system-build 记录这些字段以及 source_commit、toolchain_sha256、sysroot_sha256、dependency_lock_sha256、sdk_components_sha256、image_sha256、partition_sha256 和 factory_application（commit/version/sha256）。签名系统清单通过 build_provenance_sha256 绑定该文件。

sdk-components.json 使用 schema_version=1、sdk_baseline、manifest_sha256 和非空 components 列表；每组件记录 path、完整 commit 和 local_inputs_sha256。私有输入快照内容留在受控 SDK 仓库/构建存储，公开包只包含关联哈希和允许公开的组件元数据。仅记录哈希并不自动证明快照可恢复，source_audit 验收必须核对实际归档。

### 后续交付前置项

本批修改不等于已完成独立源码迁移、密钥部署、设备登记、签名系统镜像或实机验收。按来源迁移 → 专用签名体系 → 真实基线 → 镜像与实机验收 → 统一 Release 的顺序继续。实机/远端未执行时仍标 not_run，缺输入标 blocked。


## 2026-10-12 真机续验约束

- 用户要求全部必需真机验收无问题后才上传；本轮源码推送与草稿附件同样受此限制。
- 首台 PCB 为用户确认的 A133 MainBoard V1.2。Tina 的 B6 构建目标、sun50iw10/a100 设备树字符串与实际 PCB 标识分别记录。
- HDMI 默认关闭，仅对应验收短时开启。必须确认 capture=false，而不只看显示开关；当前已安装采集服务关闭显示后仍可能持续采集，需按完整系统修复验收。
- 新的 check-enrollment --device <descriptor> --enrollment <record> 只读取登记和内核证据，不激活更新；输入须 root 拥有且不可被普通用户改写。
- 专用签名材料已在构建主机源码仓库外准备，私钥不得进入候选或 SDK 补丁仓库；设备信任安装、历史材料轮换和异机恢复备份仍需独立记录。
- 组件快照只覆盖 repo 中已记录的差异与未跟踪输入；SDK 根目录、被忽略输入及实际恢复构建审核完成之前，不得把其标为完整可复现 SDK。
- 本次板端回归与诊断不替代真实桌面升级、回退、断电和数据保留验收；当前稳定性失败阻止上传。实际进度见 CURRENT_PROGRESS.md 第四十四节。


## 软件构建与附件自动组装（2026-10-12）

- `build_application.py` 串联干净应用源码、公开审计、冻结接口、锁定应用 SDK、桌面编译、device Web 构建、资源整理、依赖锁及 SBOM。构建目录必须在源码外；失败保留 BUILD-INCOMPLETE 和日志。它不重建 Tina、不签名、不上传。
- `application-build.yml` 在独立 `mimi-build` runner 上执行上述流程，只读源码、无发布令牌。需预装 Python 3.11、Node 22、npm、CMake、pkg-config 和主机依赖；SDK 位于 `/srv/mimi-sdks/<sdk_id>`，外部预期哈希必须来自审核过的 SDK 记录。产物保留在 `/srv/mimi-builds/`，尚未配置或触发真实远端 runner。
- `prepare_release.py --inputs PLAN.json --output NEW_DIRECTORY --checker HOST_CHECKER --trust EXTERNAL_TRUST` 组装已经签名的应用或系统产物。输入计划严格包含 schema_version=1、release_type、device_files 和 assets；每个 assets 条目为 `{ "path": "输入路径", "sha256": "外部预期哈希" }`，相对路径以计划文件为基准。上传文件名必须为单层文件名。
- 应用必须提供 application.tar.gz、manifest.json/.sig、application-build.json、sdk-lock.json、dependency-lock.json；系统必须提供 system.img.gz、system-manifest.json/.sig、system-build.json、sdk-components.json、factory-manifest.json/.sig、dependency-lock.json，声明 OTA 时还须 system.swu。两者均需 source.tar.gz、sbom.spdx.json、platform-contract-lock.json 和 device_files 列举的设备描述。
- 有真实验收报告时提供 validation.json 及其引用的证据附件；缺失时自动生成全部 not_run 的报告，不能用于上传。构建锁从签名清单和实际构建记录生成，组装前后核对输入哈希，再执行共用签名、匹配和来源门禁；失败不留下可用候选，不覆盖已有目录。
- `publish_draft.py` 和 `promote_release.py` 在任何 GitHub 请求前要求每个目标全部验收 passed；远端下载后再次执行相同要求。该附加上传限制不改变测试通道和稳定通道的类型，也不把本地候选标为验收通过。

## 独立系统版本及专用 SWUpdate 签名

- 系统构建使用 `scripts/build_release.sh --system-version X.Y.Z ... SDK`，系统标签为 system-a133-vX.Y.Z；应用 VERSION 不随之修改。启用应用更新基线时，设备描述中的 runtime.system 必须与系统版本一致。
- `--with-ota` 需要仓库外的 MIMI_SWU_SIGNING_KEY、MIMI_SWU_PUBLIC_KEY、MIMI_SYSTEM_VERSION 和固定 SOURCE_DATE_EPOCH；加密私钥另设 MIMI_SWU_PASSWORD_FILE。私钥/口令文件权限须 0600 或更严格，禁止放在产品、SDK 或产物树内。
- 平台包从 MIMI_SWU_PUBLIC_KEY 选定镜像内验证公钥，纳入依赖变化检测；SWU 打包前还会从实际 ext4 rootfs 读取公钥比对。
- `swu_package.py` 使用外部 RSA-3072 以上密钥，生成厂商同格式 crc CPIO（sw-description、签名、kernel、rootfs），保留原 A/B 目标和回退模板；不修改 SDK 配置、不把私钥复制进 SDK。产物为 `build/swu/<系统版本>/system.swu` 与 swu-build.json，记录镜像输入和归档哈希。
- 主机签名/归档验证不替代设备 swupdate -c、A/B 升级、回退、断电与数据保留验收；这些仍须对最终镜像实际执行。


## 开发目录与 SDK 恢复核对（2026-10-13）

- 用户要求所有候选源码、SDK 快照和交付输入保存在 `/home/ubuntu/AI-DeskTopBox/.delivery/`；该目录属于开发工作区，不能当作可随意删除的缓存。Git 忽略规则和公开审计同时禁止将其内容导出。签名私钥按原门禁保存在源码树外。
- `sdk_snapshot.py --snapshot PRIVATE_SNAPSHOT --expected-sha256 PIN --sdk SDK --output NEW_PRIVATE_REPORT` 校验外部固定的组件索引、差异补丁、未跟踪归档及当前源码，使用独立 Git 索引和对象目录重放补丁，不改写原 SDK 的索引、对象或工作区。
- 报告核对归档内容、权限、链接、路径与组件提交，并列出绝对链接及未捕获的忽略项；拒绝摘要错误、变化的输入、重复条目、路径越界和特殊文件。报告含私有输入路径，仅存本地私有交付资料，不上传统一公共 Releases。
- 补丁重放通过只说明组件差异可应用。SDK 根目录、有效配置、启动/打包输入、路径迁移、外部依赖和完整恢复构建仍须逐项验收；工具始终将完整恢复和镜像构建标为 not_run，不能自动给出发布资格。
- 旧旁置目录已由用户删除。旧提交及构建记录保留为历史；重建候选必须重新固定源码、SDK 哈希并执行构建与测试，不沿用已不存在的产物。

## 最终系统文件核对（2026-10-13）

- `--application-updates` 向固件构建传递强制基线要求；缺少已准备基线时，在修改 SDK 前失败。
- 产品包开关和增量清理显式包含 `aitvbox-mimi-update`，同时清理旧信任、工厂应用和启动链接；不清理用户 overlay 数据。旧版 suite 通过依赖带入更新器，但仅关闭 suite 不能保证增量残留被清除。
- 更新器系统包禁用签名后的 ELF strip，文件裁剪必须发生在应用打包签名前。包依赖跟踪更新器、基线目录及启动文件，避免复用旧输入。
- OpenWrt rc.d 按字符串排序；使用 `S98mimi-update` → `S99aitvbox` → `S99mimi-desktop`，不使用会排到前面的 S100。
- `baseline.py verify-rootfs --output PREPARED_BASELINE --rootfs EXTRACTED_FINAL_ROOTFS --image FINAL_ROOTFS_IMAGE` 核对最终镜像读回的工厂文件、更新器、信任和配置的字节/模式、启动器及启用链接，拒绝链接逃逸、额外未签名文件和机器专属登记。固件入口对 ext4/SquashFS 的实际读回调用该检查。
- 镜像读回检查不替代 prepare 阶段签名校验、真实板卡登记或实机升级验收。测试中构造的小型文件系统和 Make 安装夹具均只计为主机测试。

- 桌面启动入口通过 `Engine.StartSelected` 在同一应用/系统事务锁内完成选择、验签和平台启动；启动失败释放锁，验签失败不调用平台启动。不能先 Select 后释放锁再单独激活。

- 最终镜像检查直接读取 ext4/SquashFS inode 元数据，要求受保护文件及父目录 uid/gid 均为 0；不把普通账户解包后的属主当作镜像属主。CI 安装 fakeroot、e2fsprogs 和 squashfs-tools，覆盖错误文件/目录属主的拒绝。

## 系统构建记录自动整理（2026-10-13）

- 开启 `--application-updates` 的系统构建归档已核验的基线锁、工厂应用清单/签名和无单机登记号的设备描述；构建前后检查源码状态及基线锁不变。构建清单分别记录应用、系统版本和完整源码状态，未跟踪源码同样视为 dirty。
- `system_record.py --build COMPLETED_BUILD --inputs PRIVATE_PLAN.json --output NEW_DIRECTORY --checker HOST_CHECKER --trust EXTERNAL_TRUST` 从成功构建整理系统发布输入。要求 BUILD-COMPLETE、无 BUILD-INCOMPLETE、干净源码、A133/B6/UART0 构建目标、正确系统标签、签名工厂基线及受外部 SHA256 固定的构建清单。B6 仅为 SDK 目标，真实 PCB 仍由已签名兼容清单和受控设备登记匹配。
- 计划严格包含 schema_version=1、build_manifest_sha256、hardware_commit、sdk_baseline、sdk_local_inputs_sha256、toolchain_sha256、sysroot_sha256、partition_sha256、inputs。所有 SHA256 和私有硬件完整提交来自已审核构建输入，不在发布现场从任意文件生成后自行视为合格。inputs 仅包含 sdk-components.json 和 dependency-lock.json，条目格式为 {path, sha256}，路径相对计划文件。私有 SDK 输入闭合、分区几何与源码审计仍须实际核对。
- public SDK inventory 仅允许 schema_version、sdk_baseline、manifest_sha256、components；每组件仅 path、commit、local_inputs_sha256。拒绝私有补丁、归档路径及未知字段，且 SDK manifest 哈希必须与实际构建清单相同。
- 工具读取构建校验和，复核完整镜像、启动链门禁日志与工厂基线附件；独立验签工厂应用，核对最终封存的文件摘要。临时匹配描述只用于主机校验，不产生或安装设备登记。镜像使用固定 gzip 时间戳压缩，并解压读回核对原镜像哈希。
- 输出仅 system.img.gz、system-build.json、公开 SDK 清单、依赖锁及工厂应用清单/签名。system-build.json 额外记录 system_version、build_manifest_sha256、raw_image_sha256；后续发布门禁强制核对系统构建版本和签名系统清单版本相同。
- 这些输出尚未签署系统清单、未生成通过验收、没有上传资格；签名系统清单后继续通过 prepare_release.py 组装来源和实际验收资料。构建日志与本地私有计划不自动复制到公开产物。

## 文件系统完整性前置检查（2026-10-13）

- 固件 pack 前调用 filesystem_image.py，对 ext2/3/4 执行 e2fsck -f -n；只允许返回 0，禁止自动修复输入后仍声称原镜像通过。SquashFS 先读超级块，后续继续实际文件读回/基线核对。pack 后 rootfs 摘要必须与检查时相同。
- 实际恢复 SDK 的 make_ext4fs 2020-01-05 版本未设置 inode bitmap 尾部保留位，基础 rootfs 虽编译返回 0，但只读 e2fsck 返回 4。修复存入私有 SDK 增量补丁，不能通过关闭检查或忽略错误放行。该修复不涉及 Boot0/U-Boot/BL31。
- 主机测试以真实 ext4 镜像验证正常通过、损坏拒绝、检查前后字节不变；不代表板端文件系统已修复或新镜像已经启动。

- 用户于 2026-10-13 最新要求优先上传 100askTeam/A133-AIDesktop-Tina5 的底层驱动和板级适配差异。本次允许完成源码重放及敏感内容核对后上传私有 SDK 源码归档；不扩大到应用源码、升级包、系统镜像或公开 Release，这些仍等待全部必需真机验收。归档未测项目如实标 not_run，不标产品发布合格。

- 用户于 2026-10-13 明确 SDK 差异必须沿用私有仓库根目录 repo overlay 形式：overlay/、meta/ 和 scripts/apply_overlay.sh；包含实际修改/新增源码、配置、下载包、固件包并展开未追踪目录。排除 out/bak/project/tools/OpenixCLI/tools/serial_agent/prebuilt/rootfsbuilt/.local_patch 和编译缓存；openwrt/target、openwrt/openwrt/target 保留。不得用另一套组件 patch 目录替代该使用入口。

## 应用源码归属检查（2026-10-13）

见 [源码归属与迁移边界](SOURCE-OWNERSHIP.md)。应用构建检查逐文件归属及真实编译清单，拒绝平台服务进入应用；过渡硬件客户端和共享实现固定哈希。应用发布新增必需附件 source-ownership.json，构建回执和发布门禁核对其 SHA-256。旧回执缺失此项须重新构建，不能沿用旧包验收。平台源码物理迁移仍未完成。
