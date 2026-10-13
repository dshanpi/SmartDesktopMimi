# SmartDesktopMimi 更新机制：实现与验收

本文件补充 `MIMI-UPDATE-001`，不替代实机验收。
当前实现属于开发候选，尚未完成系统镜像与 GitHub Release 发布。

## 代码与进程边界

- `services/mimi-update`：仅依赖 Go 标准库，打包、设备与发布工具共用同一匹配实现；A133 使用 Linux/arm64 静态程序。
- 设备 Unix HTTP 接口：`/run/smartdesktopmimi/update.sock`，仅 root 可访问。
  `GET /status`；`POST /check`、`/download`、`/install`、`/cancel`；`PUT /policy`。
  修改操作异步返回 202，冲突返回 409，安装激活阶段不可取消。
- 桌面通过设置中的“桌面应用更新”访问服务；Web 使用原有认证和同源校验保护的
  `/api/desktop-update/:operation`，不允许提交任意下载地址或命令。
- 独立 `mimi-desktop` procd 实例管理已验证桌面，原 `aitvbox` 实例继续管理硬件后端。
  没有 `updater.json` 或尚无硬件登记文件时继续使用原启动路径；存在登记但验证失败时拒绝新版本启动，不能回退到不校验身份的激活入口。
- 已登记系统拒绝旧版不带目标清单的 SWU 应用请求；新的系统确认操作在同一更新锁内
  检查应用兼容性、界面心跳和后端响应，再提交 A/B 状态。
  系统 OTA 已增加签名候选筛选、下载和安装桥接候选实现；未完成真实系统基线和板端升级验收。

## 平台基线准备

系统发布必须提供：

1. 更新器和两个 init 脚本、root-owned Provider。
2. `/etc/smartdesktopmimi/device.json`：本系统的 SKU、CPU/ABI、运行库、能力、Provider、
   Ports/IPC、数据版本及系统基线；随当前 rootfs 切换，不能使用持久分区中的旧系统描述。
3. `/etc/smartdesktopmimi/trust.json`：`schema_version: 1`、`keys`（key ID 到 RSA 公钥 PEM）、
   `revoked`、`min_sequence`。私钥不进入设备或仓库。
4. 独立持久保存的 `/overlay/smartdesktopmimi-platform/enrollment.json`：受控登记的
   `enrollment_id`、`identity_sha256` 和 board/silicon 两类实际内核硬件节点证据。
   硬件摘要为设备 identity 前七项（product/platform/soc/board/revision/variant/architecture）
   按键排序的紧凑 JSON 的 SHA-256；不包含会随系统更新改变的运行库/系统版本。
   每条 evidence 为 `kind`、`path`、`sha256`，只接受解析到 sysfs 硬件或设备树中的非空内容。
   不允许把两个普通文件的内容比较当作硬件证明。
5. 已签名且兼容的工厂应用目录 `/usr/lib/smartdesktopmimi/factory`，其中包含
   `manifest.json`、`manifest.sig` 和清单中的全部文件。
6. 最后才安装 `updater.json`（格式见同目录示例），启用新启动路径。

设备描述的 enrollment_id 在运行时由持久登记记录补入。该机制要求制造/维护阶段确认
真实硬件修订；本仓库没有给未知 A133 修订、T527 或 RK3576 伪造登记数据。
首次基线安装和跨版本真实升级仍需要板端验收。

## 应用构建与签名

```sh
scripts/build_mimi_updater.sh
cd services/mimi-update
go test -race ./...
go build -o ../../build/mimi-update/mimi-update-host ./cmd/mimi-update
```

`tools/update/application_sdk.py export` 从指定 toolchain/sysroot 生成有文件级锁定的应用 SDK；
`verify` 检查全部文件与链接；`build` 只构建 `lvglsim`，不构建 Tina 或修改系统配置。
SDK 构建强制 `MIMI_APPLICATION_ONLY=ON`，不创建 lv_backend 目标；已有构建目录也会重新配置。
SDK 仍可能含受限运行库，不能因已导出就默认允许公开分发。

使用 `mimi-update-host pack --source TREE --manifest TEMPLATE --key EXTERNAL_KEY
--output NEW_DIRECTORY --epoch SOURCE_DATE_EPOCH` 生成可复验的升级包。
模板遵循 `application-manifest.schema.json`，包大小、文件清单和哈希由打包器填写；
它会检查 ELF 架构、加载器及每个 SONAME 的声明。
允许目录只有 `bin/lvglsim`、`fonts/`、`resources/`、`www/`、`lib/`。
不接受符号链接、设备文件、目录逃逸、覆盖底层服务或配置。
当前格式最多压缩 256 MiB、展开 512 MiB、20,000 个文件，实际空间不足会提前拒绝。

签名为 RSA（至少 3072 位）PKCS#1 v1.5 / SHA-256，覆盖原始 manifest 字节。
信任链由系统提供；新公钥先由独立系统发布植入，完成工厂/恢复版本更新后才撤销旧钥。
升级序号和已失败候选持久记录，普通安装不能通过回退路径降级。
当前只允许数据 schema 不变的更新，不实现不可逆数据迁移。

## 事务和启动验证

应用包在 `/overlay/smartdesktopmimi/releases/` 暂存，清单校验完成后才停止桌面。
日志采用 fsync + rename；启动前按当前系统重新匹配，并验签、核对已安装文件。
新桌面有 60 秒验证窗口；连续主循环心跳、真实 V1 后端状态响应、当前可执行文件路径
及存在 Web 资源时的实际 HTTP 内容共同决定是否提交。失败恢复旧版或工厂版本。
系统切换/回退同样经过启动匹配；不兼容应用不能直接沿用。

检查更新默认 stable，启动后随机延迟检查，随后每 24 小时加随机延迟检查并提示。
`auto_download` 和 `install_when_idle` 默认为 false。Web 设置提供自动下载、空闲安装及稳定／测试通道选择；策略原子保存到更新器数据目录，重启及离线入口采用同一策略。更新进行中拒绝改策略，通道切换立即清除旧候选。空闲检测不完整时拒绝自动安装；一次安装失败后不会每分钟反复尝试。
ETag 缓存尚未接入。
离线安装使用 `install-offline`，执行同一签名、兼容性、空间、解包及恢复检查。
草稿不会成为设备更新候选，设备不保存 GitHub 管理令牌。

## 发布门禁与证据

`tools/update/release_gate.py` 接收外部 `--trust` 与共用 `--checker`，核验附件哈希、
提交/标签、目标、契约、源码/SDK 锁定及验收报告。不能从待发布附件自取信任根。
应用、系统统一使用 dshanpi/SmartDesktopMimi，按类型与标签区分；源标签必须已固定，附件不覆盖。
`publish_draft.py` 仅上传草稿，并重新下载逐个核验远端内容。

应用与系统发布还需要填入真实工具链/sysroot/依赖哈希、受审计源码包、SBOM、
完整设备目标和绑定实际产物 SHA-256 的 validation.json。
当前开发工作区没有这些完整发布输入，因此没有上传空草稿或宣称已发布。

UI 目录登记 8 个内置应用、55 个交互源文件。`ui_catalog.py` 对新增页面/回调变更设门禁；
目录检查不等于操作已经测试。`functional-checklist.json` 保留未执行功能。
物理触摸、Wi-Fi/蓝牙/语音/HDMI、20 次冷启动、100 轮导航、8 小时运行、实机升级回退和
物理断电结果必须另行完成。主机上的进程异常退出测试不得改写成物理断电 passed。

## 基线和应用产物准备

`application_tree.py` 从独立 SDK 编译的桌面和 Web device 构建整理应用目录，
仅包含桌面、Web、字体、资源和通知文件，不复制平台后端或设备配置。
`mimi-update pack` 仍执行 ELF、签名目标和包内容检查；整理目录本身不是可发布的已签名包。
资源许可与源码来源仍须在正式 SBOM/发布审核中确认。

`baseline.py prepare` 使用完整设备描述、外部平台公钥、已签名应用包和 AArch64 更新器
生成 `build/mimi-baseline/rootfs` 及文件级锁定。准备过程执行共用验签、兼容匹配和安全解包。
通用镜像不内置每台设备的 enrollment_id 或 overlay 凭据；受控登记独立完成。
OpenWrt 的 `aitvbox-mimi-update` 包校验准备目录后集成它，
`build_release.sh --application-updates --check-only SDK` 会在缺少基线时阻断。
目录存在且通过锁定检查不等于真实板型登记、刷机或升级验收通过。

```sh
python3 tools/update/application_tree.py --desktop build/application-only/bin/lvglsim --build-receipt build/application-only/application-build.json \
  --web apps/ipkvm/upstream/static --output build/application-tree
# 使用完整模板和外部私钥执行 mimi-update pack，再准备系统工厂目录：
python3 tools/update/baseline.py prepare --output build/mimi-baseline \
  --device /受控平台输入/device.json --trust /受控平台输入/trust.json \
  --application /已签名应用包目录 --updater build/mimi-update/mimi-update \
  --checker build/mimi-update/mimi-update-host
scripts/build_release.sh --system-version 1.0.0 --application-updates /path/to/TinaSDK
```

系统发布门禁还会独立核验工厂应用签名，不能用系统签名代替应用签名。

仍待完成：真实 A133 登记与签名系统基线安装、干净系统镜像构建、系统 OTA 板端验证、
独立仓库源码归属迁移与实际草稿上传、全部板端验收。主机用虚拟硬件身份生成的测试包只能
作为测试夹具，不能作为真实 A133 的候选升级包发布。


## 系统 OTA 候选实现（2026-10-11）

- `system-check`、`system-download`、`system-install` 由现有系统更新页调用。
  输出有界 JSON 行（phase/version/progress），检查和下载允许取消，安装不能取消。
  页面在下载时将检查按钮改为取消，并对门禁失败、分区不明、安装后重启失败给出提示。
- 配置额外提供 `system_repository: dshanpi/SmartDesktopMimi` 和
  `system_device_file`；缺少时明确拒绝，不回退到任意 URL 或旧未配对 SWU。
  `baseline.py prepare --system-device FILE` 可将受控系统描述封入基线，清除单机登记号。
- `system-manifest.json` 的可选 `ota` 字段包含 `channel`、`package_sha256`、
  `package_bytes`、`destination_device`。`targets` 描述允许安装的源系统；
  `destination_device` 描述新 rootfs 的运行时，不能携带单机登记号或改变硬件身份/数据 schema。
  目标 system 版本必须等于系统清单版本，目标基线必须不同于源基线。
- Release 同时携带 `system.swu`、系统清单及签名、工厂应用清单及独立签名。
  完整 `system.img.gz` 和 SWU 分别绑定哈希，不能互相替代。
  下载最多 4 GiB，预留 64 MiB 空间；先完整验签、配对和校验下载，随后才准许安装。
- 工厂应用按目标运行时验证，不再按源系统运行时误判。当前持久应用也必须同时兼容
  源/目标系统；否则先通过应用发布升级到过渡兼容版本，再更新系统。
- A133 Provider 校验真实 `/dev/by-name/{bootA,rootfsA,bootB,rootfsB}` 对应 p3/p4/p5/p6，
  读取 sysfs start/size，拒绝交叠、越界及容量不符。分区摘要是按上述顺序排列的
  `{name,device,start_sector,sectors}` 数组的紧凑 Go JSON SHA-256；扇区为 512 bytes。
  系统描述中的 `partition_sha256` 必须使用这一算法，不使用文件名或手工兼容判断。
  可用只读 `mimi-update system-geometry` 采集分区摘要和实际容量。
- 安装要求实际 `root=/dev/mmcblk0p4|p6` 与 boot_partition 一致，不能有重复 root 参数，
  不能已有未确认系统升级；先用 SWUpdate `-c` 校验独立签名和选中载荷，再写入非活动槽。
  老入口同样改为未知启动槽拒绝安装，拒绝 p40 等子串误匹配。
- 系统事务与应用事务共用锁；写入前持久记录序号、包哈希和 boot ID，待重启阶段阻止
  应用安装。回到旧系统时保留高水位并记录失败，不自动重装失败版本。新系统健康确认后
  才提交；写入/重启/提交中断及实际 A/B 回退仍需设备验收。
- 发布工具使用 `verify-system-ota` 再次校验 SWU 和目标工厂应用。大镜像流式上传，远端
  逐项复核字节数和 SHA-256。无真实签名基线、源码归属和实机证据时不生成正式发布声明。

以上是代码和主机验证范围，不是已完成系统安装、真实断电恢复或 GitHub 发布的证明。


## 安装前存储检查（2026-10-12）

候选更新器新增 Provider `storage-health`。应用离线/在线安装在事务锁内、准备前、
激活前和健康检查后的提交前检查；系统候选操作及实际写入前检查；系统健康确认在
检查桌面前及清除回退状态前检查。拒绝条件不会被配置开关绕过。

检查以 `/proc/self/mountinfo` 的实际挂载和设备号为准，解析更新目录的符号链接与
最长匹配挂载点，分别检查 `/` 和更新数据目录。当前 A133 Provider 仅接受可写 ext4，
要求 `/sys/dev/block/<major>:<minor>/uevent` 标识的实际块设备能读取合法且为零的
`/sys/fs/ext4/<device>/errors_count`。只读、非零错误计数、缺失/无效证据或未知文件系统
均拒绝新升级和升级确认。正常启动时选择已安装版本的入口不额外加此安装门禁。
后续只读 rootfs、overlayfs 或其他平台必须实现明确的存储检查，不能默认放行。

无基线配置时可使用独立只读诊断：

```sh
mimi-update storage-health --data-root /overlay/smartdesktopmimi
```

命令输出 JSON 和退出码；不挂载、不写入、不运行 fsck。`passed` 仅表示本次读取的
挂载状态及 EXT4 计数符合要求，不证明 eMMC/供电/内核健康或长期稳定。
EXT4 错误计数为历史状态，不因一次重启、网络恢复或应用测试通过而清除。
实际维修须在可靠的离线环境中保留数据并处理，不能对已挂载根分区强制修复。

主机回归覆盖系统/持久分区错误、只读 bind mount、未知计数、解析/路径异常、
安装初期和激活前拒绝、提交时保留 trial，以及阻止系统确认回调执行。
该存储检查候选后来完成一次 A133 临时只读验证：SHA256 `c03c2ef67d15978e1b9c927d8163a789aad2bf59a6762e71ba13ac2163451e12`，真实 ext4 目录通过，tmpfs 被拒绝。临时程序已移除；未替换安装中的更新器，未执行真实升级/回退。详见 CURRENT_PROGRESS.md 第三十六节。


## 状态读取和 Web 请求竞争优化（2026-10-12）

`GET /status` 只在复制内存状态时持有任务锁，磁盘状态、签名元数据读取和响应写出
在锁外执行；磁盘读取变慢时仍可取消下载。安装事务锁及身份/签名/存储门禁保持原义。

Web 将连接错误与操作错误分开显示，成功轮询不再清除操作失败提示；操作错误在
用户关闭或下一次操作时清除。操作开始后忽略旧状态请求的结果，避免覆盖刚保存的
策略；请求期间不另发轮询。操作被接受后保持忙碌，直到后续状态确认结束。

新增 `TestSlowStatusReadDoesNotBlockCancellation`，原实现失败，修复后通过。
真实 React 组件的浏览器测试命令为 `node tests/update-reliability.mjs`（在 Web UI
目录执行）；HTTP 响应及翻译钩子使用主机夹具，覆盖错误保留、旧响应、安装按钮
禁用与断线恢复，不代表实机升级验收。前三项在旧组件上失败，修复后四项全部通过。
测试结果和独立构建产物保存于 `output/optimization-20261012/`。

## 签名多目标索引及远端发布复核候选（2026-10-12）

新增多包签名索引、在线唯一目标选择、离线目录选择、逐包发布证据门禁和远端下载后公开工具。具体边界、使用入口和待部署条件见 [发布执行门禁](RELEASE-RUNBOOK.md)。当前尚未上线；独立仓库迁移、真实板型登记、签名基线、真实升级及断电验收仍未完成。

## 应用 SDK 固定输入和构建记录（2026-10-12）

- application_sdk.py build 强制要求 --expected-sdk-sha256，取自已选择 SDK 版本的外部锁定记录；不能在发布自动化中现场取当前 SDK 哈希冒充外部预期。
- 构建前后核对 SDK 文件与预期哈希；拒绝缺少真实工具链/sysroot 目录、特殊文件、重复 JSON 字段和未知平台/基线。保留内部相对链接规则。
- 构建成功后产生 build/application-only/application-build.json，关联 SDK 清单、系统基线、接口契约、源码提交/dirty 状态、编译命令清单及桌面程序哈希；应用构建不得混入后端编译单元。
- application_tree.py 强制要求 --build-receipt，整理前后检查程序和接口，复制到 resources/application-build.json，后续应用包签名覆盖该文件。
- 构建记录不是签名、完整源码快照或实机验收；source.dirty=true 与未验收开发基线不可作为稳定发布依据。
- 本批实际固定 SDK 为 bb78f59b8a4b2128123d5601fc782bc5360c9be567ecd4ad3979f1b000482261，A133 编译输出仍为 80b224252668f4ff013ac2475f04753f187089abce1a855d11cd1e40e154ec96。源码未改桌面功能，仅加强构建与整理环节。
- 30 项 Python 测试、真实 A133 编译、45 文件应用目录及两个错误输入 CLI 拒绝检查通过。没有安装设备、重建系统镜像或执行 GitHub 发布。证据 output/sdk-build-gates-20261012/。

## 统一公开 Release 入口（2026-10-12）

应用、系统都使用 dshanpi/SmartDesktopMimi。应用 vX.Y.Z；系统 system-a133-vX.Y.Z。配置允许相同仓库，设备按类型/平台预筛选并验签，发布门禁核对源码/SDK/构建记录与签名产物；系统来源独立绑定私有 SDK 补丁提交。字段及使用约定见 RELEASE-RUNBOOK.md 的最新统一入口章节。旧系统无平台前缀标签不会作为新系统更新候选，不移动历史标签。


## 五十、应用来源边界与首个签名候选（2026-10-13）

- passed：增加 95 个边界文件的归属清单，应用编译审计拒绝混入平台服务或未登记来源；存量硬件客户端哈希冻结。源码物理迁移仍未完成，不能把审计通过当作拆仓全部结束。
- passed：应用源码提交 `0663b3615adf9a0d999a45e17a77a84f215f6c20` 上，更新 90 项、架构 33 项、平台主机 73 项测试通过。首次平台回归曾因拆分遗漏涂鸦 DP 配置/验证脚本失败，已按原文件恢复后重跑通过；失败日志保留。
- passed：SDK 97 组件快照重新检查发现新增 VIN 源码归档，旧快照保留；新输入索引 SHA256 `9ca3a5310b1a5f8b58bc7a0108f97cf51823fec8a923deaafb7b925efe2b4c32`，97 组件重放及 57 个补充输入读回通过。不是完整 SDK 输入资格或产品镜像验收通过。
- passed（仅代码/主机构建）：HDMI 默认按消费者需求启动采集，显示、视频流和快照全部退出后释放；后台监督线程避免关闭显示阻塞 IPC。A133 编译通过，二进制 SHA256 `74ceb07ac95188588617164564f2d119d18be2897b07ccf03fec777df275281a`；未部署，不能宣称已解决实机失联。
- passed：干净源码 `b2efaf0366bdebb3aad194ef1c6b7263c86bd327` 的 `1.0.8-rc.1` 桌面/Web 候选构建完成，主机及 A133 更新器编译完成。生产打包器生成 28 文件的签名应用候选，包 SHA256 `09bb56f9ccc95452d3a180ba425bba5e284a7ae381c1dd79f0477988f0fd539f`；签名、实际归档哈希/权限/内容读回通过，篡改签名与未登记设备被拒绝。
- 候选目标描述是计划中的首个 A133 MainBoard V1.2 系统基线，运行库来源/版本/哈希单独记录；设备登记保持空，不填虚假登记 ID 绕过验收。签名不代表真实目标兼容已通过。
- failed/unresolved：本轮板子一次授权 MCP 断电恢复后再次无串口响应，停止 HDMI 命令没有得到执行确认。未安装软件/刷机，串口 serial-1 已关闭；稳定性仍未解决。
- not_run：正式设备登记和信任部署、签名工厂基线集成、完整产品镜像、真实升级/回退/断电/数据保留、发布运行器部署、应用源码与 Release 上传及附件远端复验。未创建应用标签或公开发布。
- 用户已确认专用签名直接继续；独立离线介质交接不再作为开发前置。现有密钥、加密备份和历史恢复证据保留，私钥不进入源码。
- 证据：`output/system-integration-20261013/factory-application-result.json`、`source-signing-batch.json` 和本批构建/测试日志。历史测试仍绑定原提交，不重标到最新文档提交。


## 五十一、A133 真实应用升级与恢复（2026-10-13）

- 本批设备为 LYNX project 4/device 4、A133 MainBoard V1.2。用户已停止其他操作；HDMI 仅短时验收，默认关闭。未刷系统镜像、未上传应用或 Release。
- passed（受控单板部署）：读取内核硬件证据、登记实际设备，逐字节核对 7 个运行库；部署专用信任、独立更新器及签名 rc.1 工厂应用。属于受控平台安装，不等于完整产品镜像/系统 OTA 验收。
- 实机发现旧 backend 缺少完整活动状态；修复后保留现有私有云功能。更新器健康检查增加 activity_ready 必须为真；8 项健康边界及 Go 回归通过。更新器 SHA256 `072cf8d85439e36dd6b887c441de2059f090b3d5407c334b99f486c37c8b42f8`，后端 SHA256 `8a8b3a45cc8eb17f212288b149157e529891e7fd045dfe2a7c7979191514c3f3`。
- passed：签名 rc.1 → rc.2 真实安装提交；rc.2 来源 `8b06a6b6de52b90638b99850229eb85415b765ee`，包 SHA256 `d2b949fa271c931837d17829ffc76026144a75b32f78b9e837bb099c93cc96ad`。11 类错配/签名篡改、坏包、重放及并发锁在真机拒绝，旧桌面保持运行。
- passed：实际 rc.3 新进程启动后注入启动失败，生产更新器健康超时自动退回 rc.2，回退健康通过；不是只修改事务日志模拟。
- passed（限定条件）：rc.4 已写 trial 日志后暂停安装进程，MCP 继电器真实断电 30 秒，启动时自动回 rc.2，两个 ext4 错误计数为零。断电前临时 CPU 上限 1008000 kHz；重启后恢复默认 1512000 kHz。其余事务阶段未做，默认条件下稳定性未通过。
- passed（限定范围）：断电后 23 个抽查持久文件中 22 个哈希/权限一致；涂鸦运行数据库有变化，语义保留未核验，不能标全部数据验收通过。
- passed（限定范围）：恢复 HDMI worker 后默认无采集，三轮短时 DISPLAY 开关帧持续增加，关闭均释放 /dev/video0。通过的是消费者开关/资源释放，不替代物理触摸、画质、长期稳定性。
- failed/unresolved：默认频率下曾多次传包后整机串口/网络失联，即使 HDMI worker 暂停且无人操作仍复现。临时限频下能完成一项恢复测试，不构成已解决，也没有推定供电或 CPU 是根因。
- failed → 修复待验：真机首页“更新”仍打开系统固件页，应用更新入口原在设置中。已修正首页路由并命名“桌面更新”，rc.5 正在构建，待签名安装和页面回放验证。
- not_run：完整集成镜像与系统 OTA、应用/系统双向实机兼容、其他断电阶段、完整功能/物理输入与长期稳定性、发布运行器和远端附件复验。整体未完成，不输出 COMPLETE。
- 本批证据：`output/physical-acceptance-20261013/progress.json`、`rc3-startup-rollback.json`、`rc4-powercut-result.json`、`native-negative-tests.json`、`hdmi-demand-cycles.json`。关键原程序/数据备份位于受限 `.delivery/physical-backup-20261013`，未复制 SDK/编译缓存。


- 五十一补充：rc.5 来源 `16ce4d2d86fc0607624e9d216e3b1fd332d52b76`，实际包 SHA256 `cddad85c047337a2becd10e9a937c3125158fde75b672353658612779e517b43`；A133 编译、生产签名/校验和真机安装提交通过。传包/安装采用临时 1008000 kHz，上线后恢复 1512000 kHz。首页坐标回放进入桌面更新页，显示 1.0.8-rc.5；检查返回 idle、无错误、无候选，关闭后完整桌面健康通过。不是 GitHub 在线下载安装验收，也不是物理触摸验收。证据 `rc5-installed-state.json`、`rc5-ui-check.json`、`rc5-desktop-update.png`。


## 2026-10-13 接续验收与下一阶段门禁

rc.6 应用来源 0558589f622d35ee8e8d2936c673329bb870db19，包 SHA256 7b8ec1c17b09a9bdde79603da28c7d5e31393586e1db4959c325a7a7ffc7bf8b。默认频率下本地签名安装提交和提交后继电器冷启动通过。另两项限速/临时限频条件下的解包阶段断电恢复通过，不代替全部默认条件断电验收。快速网络写 tmpfs 仍可失联，涂鸦数据库语义保留未核验。

更新器 IPK 已补齐工厂桌面运行依赖，真实 SDK 包与 37 个基线文件读回通过。完整发布入口 r3 因 SDK 输出配置缺失失败；随后补回配置和哈希固定的黄金启动镜像后，窄 pack、认证启动链门禁、实际 rootfs 签名工厂 rc.6 验证通过，完整入口仍需重新跑。构建源码 bb82184ee41027431dad6493d115c96df567a725，镜像 SHA256 3b9dfce7120ec660e380099a134d2eb1a0b8535816e4a4bc39406b5ea815cce7；未刷机，未上传，不是发布合格。

新增五项功能仅登记于 ../next-phase-ai-desktop-requirements.md：先完成本轮交付，再提供 UI 效果图与业务功能设计，用户确认后才实现。
