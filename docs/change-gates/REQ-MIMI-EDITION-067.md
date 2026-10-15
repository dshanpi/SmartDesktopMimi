# REQ-MIMI-EDITION-067 产品来源锁与共享更新保留边界
2026-10-15 实施前记录，关联EDITION062、PRIVATE065/066、UPDATE001和TRACE001。

## 背景和范围
PRIVATE066已实际编译并接通公共/私有Provider、Tuya和打包，但来源只分散在多个回执。建立明确的platform-product-lock，将实际程序/来源回执与固定的公共、私有、SDK overlay提交及SDK配方/工具链输入绑定。它是系统产品锁的组件部分，缺少完整镜像和全部SDK清单时不能称最终系统交付锁。普通应用仍共用一份签名更新，不能覆盖平台/私有接入/设备身份。

流程：核对三处固定Git来源与原SDK preflight → 检查实际程序和已存在的来源回执 → 将源文件与Git blob逐字节配对 → 生成排除凭据和绝对路径的不可覆盖组件锁 → 系统构建入口归档该锁/摘要 → 后续完整系统构建记录与签名来源锁关联。缺输入、脏跟踪源码、仓库不符、回执或源/程序变动均拒绝；不复用旧系统镜像证明新来源。

共享更新主机流程：同一个测试签名清单/包分别用于公共与私有平台能力集合 → 真实文件系统安装/提交、健康失败回退、各既有故障点恢复 → 比较平台程序、模拟设备身份/绑定/授权、网络和应用数据的SHA/权限/对象身份 → 拒绝签名包中的平台/凭据路径。只用合成的测试数据，使用临时RSA测试钥，不读取或更换可信钥/真实设备绑定；Hooks模拟应用启动与健康，不能称真实语音/私有程序运行或物理断电。

## 回归不变量与验收
公共锁不含SmartDesktopMimi-Private或产品专有组件输入；板级SDK始终记录100askTeam/A133-AIDesktop-Tina5的提交和摘要；私有锁显式固定适配提交与来源锁；共享桌面不复制。SDK完整输入和镜像追溯另由系统流程负责，不以局部源码核对冒充97组件干净重建。既有协议/数据schema和设备匹配不改变；不因为product-edition标记相同就自动宣称系统OTA可跨产品安装。缺少后续系统版本约束时新双产品系统OTA仍未交付。

改动位置计划：scripts/lock_product_sources.py、scripts/build_release.sh、tools/update/system_record.py及构建来源校验、services/mimi-update/update/edition_preservation_test.go、对应主机测试与文档。产品源/包哈希、实际源提交和证据保存在output/product-edition-20261015；实施/验证当前not_run。原rc2/rc23与25草稿不变，A133物理按用户后置，HDMI不操作。

额外核对：OpenWrt安装会strip程序，原始ELF哈希不能直接当rootfs哈希。本批锁同时保存原始程序/来源回执与固定A133 strip工具42cba888处理后的rootfs程序SHA、大小、权限。只处理临时副本，不修改原程序；新镜像构建后必须对实际解出的rootfs复核产品标记、程序、公共排除和无逐台凭据。旧镜像不用于证明新组件完整通过。

首批代码：两项Go race测试通过，共16更新/回退/中断恢复子场景与6签名越界负例；共用同一测试签名/包，私有保护对象取实际A133 Provider/Tuya程序，合成身份/授权/数据文件SHA、模式、UID/GID、inode、mtime保持不变，公共未创建私有路径。Hooks仍模拟启动健康，不记实体运行或断电通过。16项来源/系统记录主机检查passed。实际三仓Git/blob收集和新rootfs/发布来源门禁的整体回归待本代码固定后执行，完整镜像not_run。

实际278ddd4首次来源收集passed：public核对公共10/SDK68个blob，private核对公共9/SDK67/私有6个blob；所有选择源与固定Git内容/模式匹配。原始程序未重编或修改，固定strip工具派生rootfs哈希：public后端6f30483c，private后端5e3f2286、Tuyaee949bf6。两版各做ext4/SquashFS最小主机文件系统的实际构造、提取、程序/标记/无逐台凭据复核及旧程序拒绝，4组通过；不含完整系统依赖，不是产品镜像。
211完整更新、75架构0skip、Go vet/完整race回归通过；随后新增3项发布来源检查和1项CLI写前保护检查，按受影响范围补验，总计215唯一更新项。固件新增旧锁/当前程序回执不同在SDK变更前拒绝，使用实际两版程序验证。rootfs记录不能覆盖输入锁/镜像/解出目录，哈希流式读取；拒绝SUID模式、布尔schema和伪造实机合格标记。新的最终Git源快照仍需本次代码提交后重收集，旧初始锁/失败不覆盖。
失败历史：新来源发布测试夹具首次漏version字段，9个子场景报错；修正夹具后3项全部通过，原日志product-release-provenance-initial-fixture-failure.log保留，不改产品门禁。最终完整SDK库存/镜像/系统OTA产品版本匹配/设备升级保留仍未完成。
