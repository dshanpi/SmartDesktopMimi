# 首轮 A133 源码归属与迁移边界

适用 MIMI-UPDATE-001；公开应用及统一发布入口为 `dshanpi/SmartDesktopMimi`，
私有 SDK overlay 为 `100askTeam/A133-AIDesktop-Tina5`。

## 当前可执行边界

| 范围 | 归属及更新方式 |
| --- | --- |
| LVGL 桌面、内置应用界面、字体、图标、Web 静态资源 | 应用源码；随签名桌面包发布 |
| lv_backend、service_*、Wi-Fi/蓝牙/传感器/LED HAL、TPlayer | 平台服务；仅系统发布可替换 |
| apps/ipkvm/video、IPKVM Go 服务、platform 下的系统服务 | 平台服务；Web 静态文件可独立更新，服务程序不可夹入应用包 |
| 更新器及 packaging 系统启动、身份、公钥配置 | 系统基础设施；源码可公开，二进制只能经系统发布 |
| 内核、设备树、Bootloader、厂商 SDK 修改与板级配置 | 私有 SDK 的 overlay；不进入公开源码包 |
| V1/V2 协议、共享配置/日志接口 | 双方依赖；协议锁与逐文件归属锁约束，不宣称 V2 全量迁移 |

当前 `source-ownership.json` 对混合目录逐文件登记，使用四种归属：

- `application`：应用拥有的源码。
- `platform`：平台服务源码，应用编译清单中出现即拒绝。
- `shared-frozen`：两侧使用的兼容实现和头文件，当前基线固定字节。
- `platform-client-frozen`：仍链接进桌面的硬件适配客户端，当前基线固定字节。

亮度、音量、PCM、Sunxi 显示和输入适配目前尚未完全移出桌面。这些过渡文件
固定 SHA-256；修改时必须评审接口和系统基线、重新验收，不能仅重写锁后沿用旧证据。
`migration_complete=false` 明确保留，不能把登记完成当成物理拆仓完成。

## 构建及发布检查

`tools/update/source_ownership.py` 核对敏感目录文件全集与冻结哈希；新增系统源码或
删除已登记文件会失败。应用编译使用显式 SYSTEM_SOURCES，禁用私有云 Provider。
编译结束再次检查真实 compile_commands.json，拒绝平台服务、未知来源、链接别名和
仓库外输入。构建回执记录归属锁哈希；打包与发布必须携带同一份
`source-ownership.json`，并与签名负载中的构建回执对应。

平台源码留在当前受控工作候选中，下一步按服务依赖迁出；迁出前应同时核对 include、
链接依赖、安装配方和固定接口兼容。系统锁同时记录公开配方提交与私有 SDK overlay
提交，不以其中任一提交代替另一方。SDK overlay 的应用入口保持
`scripts/apply_overlay.sh /path/to/TinaSDK`，不得复制整个 SDK 到公开应用仓库。

## 尚未完成

平台源码物理迁移、应用 SDK 内的平台客户端交付、真实设备对旧后端的兼容验收，
以及最终镜像、升级、断电和发布验证仍需继续。本文不为 T527/RK3576 提供支持证明。


## SPLIT053 独立SDK sourcecheckpoint（2026-10-15）

私有SDK `958c62f739c86fcc1128fb2d838faa25c550aaa9` 已增加
`overlay/openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-provider`，
内含private/public独立source profile、真实依赖闭包配方和原SHA构建器。
两profile均已独立编译且与各自旧参考字节一致；标准overlay95基准/4248文件
重放及私有repo独立远端读回通过。实际恢复SDK新增该组件并编译private版本通过。
厂商100ask实现仍外置，公开stub不读私有依赖。

这一步没有把源码登记当完成：系统build entry尚未切换到SDK组件，应用工作候选
仍有旧平台实现和冻结客户端，最终应用SDK/业务归属也未收尾。
`migration_complete=false` 继续保留。详细证据与失败历史见SPLIT053门禁。
冻结sysrc2来源锁继续指向构建当时实际1fb输入；958为新的sourcecheckpoint，
不得倒填旧镜像、复制其真机结论或提前上传应用/Release。


### 2026-10-15 产品入口切换（REQ-MIMI-SPLIT-054）
`scripts/build_apps.sh`预检固定SDK组件后，使用SDK内独立helper构建`lv_backend`，桌面`build_openwrt.sh`只构建`lvglsim`并强制归属检查。双profile实际重编与原后端字节一致，A133上层672编译源审计passed。实际源码60b0b13，详见054及output/product-provider-20261015。旧混合CMake分支和平台源码仍保留作对照，未完成公共旧源移除；完整产品整链/新镜像及真机未执行，不把该入口检查当作迁移完成。


### 2026-10-15 旧后端C退休（REQ-MIMI-SPLIT-055，替代054“旧C仍保留”当前状态）
23个已在SDK958保存并实际重编的platform C已从桌面源码删除；24个接口头和冻结共享/客户端保留。CMake默认上层且拒绝混合模式；旧后端target/库查找已移除。SDK组件继续独立构建系统后端，provider-recipe中保存旧闭包与原字节。
所有退休路径及SHA登记source-ownership.json，重新出现即拒绝。原平台实现测试保留并从显式MIMI_A133_PROVIDER_ROOT指向固定配方与实际SDK源码；无配置对应检查明确skip/not_run，配置错误失败。实际SDK条件106架构/平台+28边界通过，A133前端672源重编且清理前后字节相同；证据output/backend-source-retirement-20261015。
剩余：平台接口头/冻结硬件客户端边界、其他apps/platform_services/hdmi_preview/ipkvm及集成材料的最终归属仍须收尾；migration_complete=false。完整新产品/升级/物理验收不因本批通过而变为passed。


054完整应用入口接续补充：实际全入口在npm未进入PATH时阶段4失败；19早拒绝/边界回归通过后，只加载已有Node工具链、逐字核对尾段并验证前序收据/SHA后接续阶段4/5及更新器，最终passed_recovered。前端/SDK后端归集及其他组件衔接已通过；原失败保留，未变模块增量构建，不称全SDK干净重建。原冻结镜像/包未变，无真机结论。证据output/product-pipeline-20261015/verified-result.json。


### 2026-10-15 五系统兼容服务迁SDK（REQ-MIMI-SPLIT-056，更新此前platform_services待办）
原apps/platform_services的8个已追踪文件已移出上层，C/H原字节与GPL许可证保存于私有SDKd9的smartdesktopmimi-a133-services组件。Makefile固定原实际GCC11，不再导入SDK.config执行配置选择，原编译链接参数保留。source-lock固定源/自含helper/203 SDK头依赖/链接库与别名/编译器/五目标参考字节；系统入口缺/变来源在清理前拒绝，不回退旧源。
实际新stage3独立编译归集的五个ELF均与原目标同字节；136主机回归（实际SDK，0skip）、AArch64 ABI和冻结候选/SDK配置保护检查passed，旧桌面672源清单复核通过。其他未变阶段沿用054实际结果，不重跑无关编译；不是全SDK干净重建或真机验收。
平台集成测试现需同时设置MIMI_A133_PROVIDER_ROOT和MIMI_A133_SERVICES_ROOT，指向对应标准SDK组件；无SDK明确not_run/skip，配置坏failclosed，不恢复旧副本。源码退休清单和retired_scopes阻止改名实现回到旧目录。
迁移系统生命周期兼容实现并不代表agentd的混合业务全部天然属于底层；受信执行/权限边与上层编排还需最终收尾，其他hdmi/ipkvm/Tuya与旧接口/冻结客户端亦未完成，migration_complete=false。当前物理与完整新镜像未验收，应用/Release未上传。证据output/platform-services-split-20261015/verified-result.json。

## SPLIT061 共享采集系统入口（2026-10-15）
SDK a49d1d1标准overlay4276/95已独立回读。原apps/ipkvm/video的5源/补丁保存在SDK smartdesktopmimi-a133-capture并从上层退休；系统产品入口先校验固定源锁，再用SDK独立新目录编译，不保留隐式回退。Web/Go业务仍在公共源码，普通Web静态资源可应用更新，采集/Go服务程序只能走系统发布。原Linaro5.3、142编译输入/28链接输入含脚本/别名及补丁结果固定，实际新video目标81,304字节/74ceb07与原参考一致。34边界+native smoke、74架构、74平台检查全过0skip，native2重复计入后唯一180项；未重编无关桌面/网页/Tuya或旧镜像。芯片/ABI/加载器/原冻结产物与SDK配置SHA复核通过；物理后置、整体迁移false。

产品开源/私有配对的新边界见EDITION-SOURCE-MAP.md，当前仅完成实际归属清单，尚未实现公共禁用/私有启用两产品隔离。

## PRIVATE065：100ask运行适配的产品来源
private Provider构建另外显式传入`--private-product-root /绝对路径/SmartDesktopMimi-Private`，或在build_apps前设置`MIMI_PRIVATE_PRODUCT_ROOT`；外置100ask依赖仍通过`--private-sdk`/`AITVBOX_100ASK_SDK_ROOT`提供。固定锁`adapters/100ask/source-lock.json`与原两C字节、原SDK配方关联；临时组合后调用原SDK编译器/审计工具。缺产品组件拒绝，不能从SDK历史副本回退。public Provider拒绝两种私有输入，30TU禁用实现不变。SDK旧副本仅历史留存，产品源码以私有组件为新来源；Tuya隔离及完整产品锁/实际设备验收不由本条宣称完成。
