# REQ-MIMI-RELEASE-072 同硬件不同运行基线的发布证据作用域

2026-10-15实施前记录，关联SYSTEM070、EDITION062/067/068与MIMI-UPDATE-001。新rc24真实签名包明确支持公开系统rc3、私有系统rc4，硬件身份相同而runtime不同。原正式prepare_release在duplicate target evidence处拒绝；不能删除一份目标或证据绕过。原失败日志output/system-product-20261015/application-local-draft-r2.log保留。

## 范围与流程
只修发布主机门禁及测试，不改变应用/系统签名、已有二进制、rc24版本/包SHA、系统镜像及设备接口。资产哈希及原完整签名/目标校验→按签名清单的完整Target身份/运行范围/能力分类证据→每份证据必须唯一对应一个声明目标→重复同一目标或未覆盖声明拒绝→保留原physical/stable/upload门禁。

唯一硬件身份旧路径保持，不新增外部执行；重复硬件身份组使用现有受控Go checker的match能力，对已通过完整验证的清单临时逐目标投影执行兼容判断。应用投影保留真实应用数据约束；系统投影仅用于目标谓词分类，完整系统/工厂/SWU验证仍独立强制。多个声明同时匹配属于歧义，拒绝；换文件名、enrollment或在同一允许范围内改版本均不能冒充另一目标的覆盖。

## 回归不变量
不能凭设备名称/公开私有标签计数，不能复制一份物理证据认证另一系统；严格JSON、签名/哈希、完整声明匹配、data和产品edition检查不放宽。不改变Go/设备程序、签名钥或受保护的设备身份。草稿not_run继续not_run，稳定/上传仍需真实验收；用户058受控快照例外独立记录，不修改正式门禁。旧rc23/rc2及25附件不覆盖，公共和私有专有产物不自动公开。

## 实现与验收
计划tools/update/release_gate.py与tests/update/test_release_target_scope.py，记录及批次/进度随源码。验收not_run：真实rc24两版本完整草稿、别名/同范围重复证据拒绝、缺少覆盖/歧义目标/错误硬件拒绝；旧发布/集合/来源/系统门禁回归。需绑定当前实际签名包c3339a52及两份host-only device，不能以模拟签名代替；现有checker91a8a73b不重编。主机构建通过不代表物理验收；所有真实安装/断电仍deferred/not_run。

## 本批实际主机结果（2026-10-15）
实际rc24两系统完整正式草稿已在临时目录通过，真实签名与包归档验证、两份明确runtime及唯一目标覆盖均执行；全部初始物理/主机证据仍not_run，stable和正式上传继续拒绝。两别名在同一允许范围（含不同enrollment和updater版本build元数据）不能冒充另一系统，缺失目标、重叠歧义、错误SoC均被拒绝。系统谓词使用真实Go匹配器并保持目标工厂载体不变，不能拿分类投影当安装授权。

受影响发布/集合/来源/系统回归共55项0skip通过，公开源码审计0发现。首调用漏设Go PATH导致集合类setup失败，原日志scope-tests-r1.log保留；修正环境后r2全部通过，不修改源码以跳过集合。新冻结包/两个镜像及可信钥未变，没有重新编译应用/系统。证据output/release-scope-20261015/scope-tests-r2.log和public-audit.json。持久本地草稿和新远端附件仍待实际执行，不标已发布。

## 2026-10-15 统一交付说明同步（实施前）
三份新草稿已经实际完成16/20/20附件完整远端下载复验；已有README仍指向旧25附件，产品文档仍称完整镜像未完成。本批仅同步中文/英文入口、产品边界、完整获取/配对/SDK接续说明与公开的复验元数据，并记录后台客户端与真机未完项。不重编/改版本/移动标签/覆盖附件，不把文档提交当产物源，不公开私有源码/真实凭据/原始串口。旧记录保留为历史。

流程：读冻结锁与实际三份复验结果→核对16+20+20/所有checks通过及草稿限定→生成公开只含哈希/关联的交付元数据→修正相对链接和源码归属说明→文档差异/链接/追溯检查→独立文档提交和源读回。任一来源或链接失败保留failed并修复，不改产物绕过。实际构建源码bedf1a8/a6e7a3b、工具34bb57b和私有755636e/overlaycd92不改写为本批文档提交。

验收当前not_run：文档字段与原复验结果一致、链接落点、变更仅文档、追溯覆盖及远端源回读。已执行远端56附件passed不重跑大包；新系统真实安装和稳定发布仍deferred/not_run。旧测试板rc17黑屏诊断仍failed_unresolved，用户补充先有开机画面后黑屏，不是本批新产物已装板。

本批文档检查：所有更新页面的相对链接/图片落点已实际检查（123项），公开56附件元数据与原远端复验结果逐字节一致；改动仅Markdown/JSON，无产品重建。首次链接检查发现README中2个旧失效路径，已更正到实际实现/平台边界文档；r1 failed及r2 passed均保留在开发证据。设备修复未通过、整体未完成；提交覆盖和远端源读回待实际执行。


## 2026-10-15T14:14:51.573980+00:00 PAIR071新59附件完整远端复验

app26/public5/private6三本地完整draft、系统34项正负例及两个源码附件passed；新59附件全部实际下载、验签/SHA/版本/源码关联passed，系统gzip/raw和下载SWU签名复验passed。source ccfe/e658/ccfe，SDK fa0d799，私有f80。原81附件/固定标签只读核对不变。前序wrong-checker/脚本变量/占位误报失败未删除；仅纠正发布主机脚本。一次受控GET接续后采用最多12并行范围，完整哈希及门禁不变。详见docs/delivery/PAIR071-SNAPSHOTS-20261015.md和docs/validation/pair071-delivery-snapshot.json。所有物理验收仍deferred/not_run；后台补丁由用户合并，未部署；不推广稳定，不将新文档提交认作二进制来源。

## 2026-10-19 Ubuntu源接口与运行目标分离（修复前）
候选c84c564编译源契约mimi-ipc-v1.2，实际Ubuntu Provider和已登记目标mimi-ipc-v1.1；完整原生候选在该Provider的5组生命周期及UI持久化已通过。发布器却在验签前强制目标IPC等于构建IPC，与MIMI-UPDATE-001第5节“构建追溯与安装兼容分别记录”冲突。
本批仅移除application分支的这项混淆检查，源码契约/receipt/锁哈希校验及原Go签名、全目标组合匹配、每个声明目标证据覆盖全部保留；系统分支不变，不给未知接口自动别名。新增来源不变、旧运行契约明示、未声明接口拒绝回归，再用原密钥给既有未改Ubuntu候选签新rc29/sequence29，只本地验签/草稿，按原设备真实描述声明唯一Ubuntu目标。错误版本必须由原签名准入拒绝；不重登记、不假称后端升级，不上传旧标签/附件。若实际兼容或数据预检失败，停止安装保留原rc22。事前证据output/ubuntu-upgrade-20261019/intent.json。

### 2026-10-19 Ubuntu SDK schema-2 接入（修复前追加）
真实 rc29 原钥签名及错误目标拒绝后，draft 被 unknown build/SDK provenance schema 拦截。现有 Ubuntu SDK 生成器与 deb 工具已使用 schema 2；发布检查仍仅接受 schema 1。本批限定支持 Debian/Ubuntu schema 2，要求 receipt 与 SDK 的 OS/版本完全相同且每个签名目标具备对应 target-os 能力；保留所有 SDK/产物/归属哈希绑定及 schema 1 兼容。未知格式、OS 不匹配、缺少签名 OS 能力必须拒绝。保留首轮失败，复用已签名包，不改 receipt/SDK/设备登记、不重签或重建。

### 2026-10-19 共享源码归属记录与运行目标（修复前追加）
schema-2 修复后第二次 draft 因 source ownership/build provenance mismatch 拒绝。生成器 source_ownership.verify 固定接受 a133 来源归属登记，Ubuntu 与 A133 共用该清单并逐编译源检查；其 platform_id 表示迁移来源，不能重标为 Ubuntu。发布层改为复用当前固定 a133 归属注册语义，继续绑定原签名 receipt 内的精确归属 SHA，不放宽 SDK/receipt/manifest 的平台与架构一致性。增加未知归属注册及错目标拒绝测试。首轮已签名包保持，第二次失败保留，尚未安装。

### 2026-10-19 Ubuntu rc29 本地签名、实际升级与重启结果
- 沿用原钥生成 Ubuntu 1.0.8-rc.29 / sequence 29，构建源 c84c564，包 a3a0159180c79445b2b8bcc76599ff1e089292e57cfeedd58a1fb07912423d1f；原 rc22/rc28 不覆盖。此次工具/测试/记录变更未触发重建，未重标候选来源。
- 发布检查区分 source IPC1.2 与已签名 runtime IPC1.1；接入已存在 schema-2 Debian/Ubuntu SDK，绑定 OS/版本与签名能力。共享 a133 来源归属仅对 schema-2 Ubuntu 明确兼容，其他平台仍保留原逐平台规则；平台/架构、签名、归属/SDK/receipt 哈希绑定不变。
- 53 release 回归（含6个实际 rc24 签名测试）和3个接口区分测试 passed。原钥8项正负验签、实际登记设备验签、真实草稿准备 passed；stable因缺验收拒绝。
- 原 Ubuntu VM 实际 rc22→rc29 激活、X11健康及同一登录内选定应用 stop/start/健康 passed；UI075 首页截图可见，desktop UID1000。settings.json 唯一现有数据文件逐字节不变，保护的平台/信任/登记文件未变，rc22保留。此项不是登出/重新登录或物理触摸验收。
- 失败历史保留：旧测试引用可变合同（按原归档哈希恢复独立输入）、SDK schema拒绝、归属误判、初稿过宽限定导致4回归失败（已收窄修复）、不支持的独立preflight命令（未执行安装且旧状态未变；实际安装自带preflight）。未删除失败或重签候选。
- 用户数据/旧登记备份只在受限本地目录，不随源码或证据公开；不改A133、信任钥和云绑定。媒体/扩展实际消费者、logout/relogin、真实语音/账号/涂鸦、新SDK物理矩阵未完；rc29尚未远端交付。
- 证据：docs/validation/release072-ubuntu-rc29-20261019.json，原始 output/ubuntu-upgrade-20261019/。下一步按DELIVERY058独立草稿同步/下载复验，再补Ubuntu消费者与登录生命周期；不宣布稳定或整体完成。
