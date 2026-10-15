# REQ-MIMI-SDK-069 最终产品SDK输入核对
2026-10-15 实施前记录。接续SDK032/038/047、PRIVATE066、EDITION067/068及MIMI-UPDATE-001。

## 范围与执行顺序
当前新产品组件与更新器已验证，但完整产品镜像尚未构建。先只读核对现有SDK97个真实组件、根/补充输入与最新标准overlay cd92e9bb，关联已保存材料和实际源文件；读取现有构建进程，不能重复启动或影响其他项目。只有输入闭包明确后，才准备最终公共/私有产品基线及新系统候选。

流程：固定公共/私有/overlay提交及现有恢复材料SHA → 逐组件核对真实Git根/HEAD与补充输入来源 → 标准overlay只读readback核对实际SDK → 比较额外变更、忽略输入及生成配置 → 一致则生成私有输入证据和可公开摘要 → 不一致则保留失败并定位，不覆盖SDK、不把已有干净构建移记到新源码 → 下一批受影响构建与完整镜像回读。

## 回归不变量
本批初始只读：不apply、不清理SDK、不修改已验证kernel/Bootloader，不替换可信钥/绑定，不操作A133或HDMI。旧rc2/rc23/25附件与原证据保持；文档不触发二进制重编。97全部SDK清单与95 overlay基线分别记录，不混淆。模拟身份/工厂基线夹具不能作为最终设备登记或完整系统镜像。公开/私有产品tag/version全局唯一候选，文件名继续SmartDesktopMimi-SystemImage-v<版本>.img.gz，内部A133匹配保留。

实施初始路径：私有output/final-sdk-inputs-20261015记录；应用工具及SDK工具只使用既有入口，若发现需要代码修复，先追加具体范围和门禁再实施。实际提交/文件/包SHA由后续证据绑定，不能提前虚构。当前完整输入核对not_run；完整新镜像not_run，真实安装/升级/回退/断电按用户deferred/not_run。当前共享源码507cf78、私有755636e、SDK cd92e9bb均跟踪源码干净。

## 实施前追加：已定位输入差异的受控恢复
初次实际readback失败：97 HEAD均一致；overlay4283中4222完全相同、60仅模式变化（600/700替代保存的664/775）、1个openwrt/target/a133/a133-b6/defconfig内容与模式不同。内容差异为旧SDK配置展开的SWUPDATE等生成项，原比对结果和diff保存在私有证据。不能将该SDK记作与overlay完全一致。

本次允许的恢复仅限上述固定61文件：先保存完整原defconfig和61份前值/哈希/权限；重新核对其实际值与初次证据一致、确认无本SDK构建进程；60项只chmod回锁定模式，defconfig仅复制已验证overlay的固定字节和模式。若路径不符、变为链接、字节变化或有本项目构建则拒绝。不执行apply全量复制、不动其他SDK文件/内核/Bootloader输出，不把这一步称新干净重建。随后调用原标准verify_overlay --readback，保留初始failed与本次修复记录。恢复材料只保存这一份配置和元数据，禁止全SDK备份。

追加只读验证方法：组合原97组件快照和最新overlay时，使用临时Git index/object目录重放原tracked.patch，逐一核对实际修改/新增/删除文件是否来自原锁定树或最新overlay；额外源不能靠HEAD一致而放行。临时Git对象不进入SDK，不跳过openwrt/target。两个忽略配置另外核对：kernel.config必须是保存材料中唯一SDK047屏幕选项修正；OpenWrt原有效配置与锁定defconfig经同一既有Kconfig元数据分别生成到临时目录，比较一致且保护输入不变。元数据未从空目录重建时明确注明，不扩大成全SDK重建结论。

组合审计初次review_required137项，未直接放行：132个GPU生成链接为旧SDK绝对路径改成SDK内相对链接、1个GPU config_kernel.mk仅两处SDK根路径迁移、4个新增项是本轮既有builder导入产生的固定__pycache__。本次逐链接验证旧绝对目标与新相对目标对应同一个SDK内原锁定源，并核对目标源已有passed内容；config_kernel.mk只能是旧根精确替换到当前根，不能接受其他字节变化。4个已列明.pyc按用户“排除编译缓存”要求记录SHA后仅unlink这四个普通文件，避免后续导入复用未锁定字节码；受控后续命令使用PYTHONDONTWRITEBYTECODE=1，不改用户级配置，不递归清理。随后逐一复核初次所有实际源字节/链接和97 HEAD、额外新增/修改集合未变，再生成单独reconciled报告，原137项历史不覆盖。

## 实际检查点结果
97个真实Git根/HEAD全部匹配原保存材料；4283个overlay条目及95基线在限定恢复后标准readback通过。临时Git索引重放原tracked.patch，4694个修改/新增/删除对象复核通过；最初137项review_required已单独核验为132个相对链接迁移、1个GPU Make变量根路径迁移、4个固定pyc缓存，原失败/待复核报告不覆盖。没有额外未知源被默认忽略。

57个已知根/忽略输入分别对应原字节、已审查根迁移/既有构建入口或两个明确配置派生；内核仅SDK047屏幕选项变化，OpenWrt当前配置和锁定种子通过同一既有Kconfig元数据归一化后均为be8633bf。保护输入未变化；元数据不是新空目录生成，不能替代完整SDK重建。97个原untracked源码包共318.1MiB和supplement原包重新计算SHA均匹配锁定材料，不重复备份这些包。

生成新的输入检查点：a133-tina5-observed-20261013-overlay-cd92e9bb-069；97组件公开摘要sdk-components.json SHA256=90b8172a71a9262a80a0d2aee74249000d7676fe4a5cc45207c19b1d14333812，私有组合描述SHA256=0ea3842bd05ef767ccd6f5691a02cfcc10434fe012e4e14f89b241b438170d86。只记录输入，不标新系统或实机合格。实际来源恢复材料、两份有效配置和逐对象证据在.delivery/final-sdk069-inputs及output/final-sdk-inputs-20261015，后续系统记录需要按实际构建再关联。

屏幕源输入门禁和原golden打包输入门禁通过；未运行pack/安装。报告驱动错误保留：pack_inputs默认输出是文本，初次误按JSON解析；另一次临时组合描述驱动误从inputs.json而非component index取archive pin，KeyError发生于任何SDK写入之前。它们均为调用方错误，不修改或归咎产品校验器；改用原--output和正确锁定字段后通过。

本批只恢复1份配置内容、60模式并清理4固定缓存；未重编任何二进制，无设备/HDMI/钥/绑定操作。223/75及Go二进制的EDITION068证据沿用原范围，不为文档重建。后续完整产品新版本必须准备兼容最终运行环境的新工厂应用/基线；冻结rc2/rc23仍不变，物理验收后置。
