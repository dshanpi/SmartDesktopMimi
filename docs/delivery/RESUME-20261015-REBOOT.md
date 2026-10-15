# 重启接续记录：2026-10-15 / PRIVATE065

用户本次明确要求先保存状态，重启电脑后再继续。长任务状态paused，原mimi自动循环PAUSED；没有创建新调度器。未经用户继续，不自动推进开发/构建或硬件操作。本记录不表示交付完成。

## 先读与位置
开发主机192.168.1.199，工程/home/ubuntu/AI-DeskTopBox。先读AGENTS.md、CURRENT_PROGRESS.md、docs/application-update-policy.md、docs/CHANGE-GATES.md和output/next-phase-20261013/progress.json，然后本记录及REQ-MIMI-PRIVATE-065。应用独立工作树.delivery/application，产品私有.delivery/private-application，硬件SDK归档.delivery/hardware，实际SDK.delivery/sdk-restored。不要在宽根脏历史仓库做全量git add/提交。
Windows当前开发目录为C:/Users/世玉轩/Documents/Codex/2026-10-12/home-ubuntu-ai-desktopbox，work/remote.py为已有SSH访问辅助；work/delivery-evidence-20261015保存上一批精简源码bundle/证据及下载SHA已核对的manifest。访问/签名秘密不写进文档或Git。

## 当前真实完成
- rc23应用和rc2系统的源码/标签及25草稿附件已上传统一SmartDesktopMimi，并全部下载核对四原RSA签名、SHA、gzip原镜像和版本关联；非稳定发布。旧附件/标签不重命名覆盖，新代码不能回填成旧镜像来源。
- SDK原标准overlay当前a49d1d11cc65e5832f77cb3cfcbd215054aa4641，已上传读回。Provider、5服务、HDMI和采集实现归属SDK；共享采集061实际程序同字节与180唯一主机检查passed。硬件工作树原有untracked meta/delivery-inputs.json、sdk-inputs/保留，不能顺手加入。
- IMAGE063新命名SmartDesktopMimi-SystemImage-v<版本>.img.gz已进入归档/系统构建记录/发布链，44针对性、184更新、74架构及相关CI passed。只是未来构建/主机夹具，新SDK完整镜像尚未重新构建；旧rc2文件名和字节不动。
- PRIVATE064生产工具已迁私有，68原源含helper/原许可全部读回一致，65公共旧入口退休并由审计拒绝。公共75架构/源码审计，私有前端构建和source/web/Rust core CI passed；原umask/README漏收及本机旧Rust锁失败历史保留。公共主线7fbe12f、私有上批ab69c99均已上传。

## 本次PRIVATE065停在哪里
两份100ask配对实现已由SDK a49的Git对象原字节保存到产品私有adapters/100ask/system/service_cloud.c及service_cloud_validation.c；锁adapters/100ask/source-lock.json SHA256为9bcd9a60ba97ecd5e020e035284f33d178ea64a35031a771839c0e731525e993。私有checker已覆盖运行锁，5原拒绝回归/两源审计通过，497468c44ad5b3f72452813f22182999b81728b0已上传，独立.delivery/private-source-readback-20261015已fetch到同一提交并审计通过。
公共树本次仅PRIVATE065实施前门禁de0dc6fb73aa547de22313f6ef3457d993e52372，暂停前远端公共main仍7fbe12f992a4906bd959548777f6e4aa2e1f2c8c；本接续文档的后续本地提交由增量备份manifest固定，未声称已远端发布。构建器scripts/build_a133_provider.py尚未修改；Windows work/build_a133_provider065.py只是从现有公共构建器下载的原稿，未打补丁。没有新Provider、桌面、镜像构建在运行，没有操作A133/HDMI。

## 重启后严格从这里继续
1. 用户说继续后再恢复原mimi循环，核对上述源码/远端HEAD、干净工作树、PRIVATE065输出和是否遗留作业，禁止重复初始化私有仓库、重传旧附件或重复开启旧构建。旧长任务描述中的上传前置已被最新用户要求覆盖，以AGENTS/本记录为准。
2. 先独立核对497468c的两原C/Git mode/锁和CI实际状态，源来自SDK a49，不能读取或加入设备secret/签名私钥。SDK旧private目录的两源暂保留历史副本，但新构建不选它们；没有隐式回退。
3. 修改公共scripts/build_a133_provider.py：private profile新增显式--private-product-root，固定校验上面的锁/SHA、恰好两文件映射、原recipe关联。private仍要求独立--private-sdk的外置100ask依赖；public profile拒绝任何私有输入并保持30TU禁用实现。两个来源均要canonical/常规文件/范围/SHA检查，在清理、输出创建和执行前失败。
4. 私有构建在新临时目录组合原SDK共享code/原recipe与私有两C，只替换明确映射，不复制桌面、不修改SDK输入。仍调用固定原SDK build_provider.py d2fc9477c990642fa27b56f65512ee751e5b3a1a478e5011b0f4878c0b56b4e0；private原recipe ab6e835a6062be717aa19c0b3634a48249a2ec77f3b4f0405b363776f65e883b，public f005cb9f5ddb614a0fa27e040e787e8838743a123792d6764b449a27eb6089e3。保留原dependency审计、__FILE__映射、编译器逻辑argv0和前后复核；回执绑定私有锁及来源。将新参数贯通build_apps的预检与编译调用，缺组件禁止从SDK旧副本补齐。
5. 增补tests/update/test_product_provider.py：缺产品适配、错锁/篡改/链接/错误profile拒绝，旧输出保留，SDK旧配对副本不可作为来源；配置原实际SDK/外置依赖后分别实际30/32TU编译，私有应与原8a8b3a45cc8eb17f212288b149157e529891e7fd045dfe2a7c7979191514c3f3同字节。只重跑受影响测试，不因文档重建旧候选。外置100ask依赖路径可先核对工程根third_party/100ask-iot-sdk的include和src，仅读受控构建输入，不扫描keys。
6. 完成PRIVATE065真实结果/失败历史、两仓提交、进度JSON、关键增量备份与源码上传回读，再处理Tuya混合源中一机一码授权初始化的私有适配/公共禁用，最后完成双产品组合构建/公共更新保留私有组件等验证。不能把100ask两源复制当完整产品隔离。

## 仍然未完成及约束
A133失联根因、真实新系统安装、升级/回退/事务断电/数据和最终功能矩阵按用户最新要求deferred/not_run，等新SDK Ubuntu环境；不要再让它们阻挡当前受控源码/草稿同步。未经验收不得公开为稳定正式版。Ubuntu真实Xorg登录用户暂无法进控制台；100ask账号归属/新绑定、真实语音和Tuya云项目/设备实控未验收；用户已允许先接入Tuya代码，资源后补，不重复索要密钥。
普通桌面/应用/通用IoT/升级统一公共SmartDesktopMimi，产品一机一码专有源归SmartDesktopMimi-Private，板级SDK仍独立100askTeam/A133-AIDesktop-Tina5标准overlay。私有仓库也不存秘密，不替换可信钥/账号绑定。SDK/分辨率/平台选择只在开发主机，不进桌面UI。原rc2/rc23冻结；HDMI默认关闭，本轮不操作设备。

重启检查点机器记录：output/private-100ask-adapter-20261015/reboot-checkpoint.json。关键增量备份的准确提交/大小/SHA见.delivery/critical-source-20261015/reboot-private065-20261015-backup.json；本地下载副本在Windows work/delivery-evidence-20261015。
