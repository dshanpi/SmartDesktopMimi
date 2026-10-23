# REQ-MIMI-SYSTEM-070 共用工厂应用和新产品基线

2026-10-15实施前记录，关联EDITION062、PRIVATE066、EDITION067/068、SDK069和MIMI-UPDATE-001。当前公共ed46c19、私有755636e、SDK overlay cd92e9bb；无本项目构建/硬件任务运行。

## 范围与目标
旧rc23只声明原系统rc1/rc2，不能挪用其兼容范围。为当前分层源码准备实际编译的共用应用1.0.8-rc.24及两个主机产品候选基线：公开系统1.0.0-rc.3、私有系统1.0.0-rc.4。系统版本独立且全局唯一，内部A133身份仍保留；新镜像名SmartDesktopMimi-SystemImage-v<版本>.img.gz。已只读确认三个远端tag均不存在，尚未创建tag或公开发布。

流程：锁定源码/SDK→干净独立应用编译与Web构建→保存实际构建记录→按两份明确系统目标产生签名应用→主机验签及拒绝未声明目标→生成明确public/private且移除单机登记的工厂基线→正式产品预检/窄目标构建→真实rootfs库及来源核对→冻结/草稿复验。任一运行库/身份/版本不匹配停止该候选，不能放宽约束；不变更旧附件。

## 回归不变量
- 公共/私有共用一份桌面应用，不把秘密、单机身份、私有SDK装入公开包；新目标为主机构建候选，不宣称设备登记/安装通过。签名使用现有外部可信钥。
- 两版正式镜像必须以实际rootfs库核对声明运行依赖，旧runtime指纹只是待核对输入，不能把旧证据移记到新镜像。只接受两个精确系统版本，不泛化到未来版本。
- 只做必要kernel/rootfs/pack，不重编启动链、不刷机、不操作HDMI；保留黄金启动链门禁和失败历史。
- 100ask后端192.168.1.17严格只读，不借本批改动后台；配对接口缺项/10分钟与1小时不一致另留PAIR020未修，不能称私有产品云验收完成。
- 文档不单独触发构建；本批VERSION/新产品输入实际变更需要真实应用构建。已完成066/067/068/069不重复验收。

## 实现位置与验收
本批源码先更新VERSION为1.0.8-rc.24和本记录/进度；实际输入、构建、签名/基线/运行库对比保存在开发目录.delivery/system070-*、output/system-product-20261015。完整新镜像尚未存在。

- not_run：新共用应用真实A133编译、Web、应用来源/契约审计与构建回执。
- not_run：现有产品钥签名、两目标验签、其他版本/身份拒绝、工厂基线移除单机登记及public/private标记封存。
- not_run：正式产品预检、真实完整镜像、运行库对比、完整SDK/产品关联和远端草稿复验。
- not_run/deferred：A133安装、失联、升级回退断电/数据，按用户等新SDK环境；Ubuntu登录、真实Tuya/语音/账号归属未验收。

后续只记录实际阶段与SHA；编译/主机验签不替代真实设备。旧rc2/rc23和25附件冻结，整套目标未完成。

### 真实package/install文件冲突修复（2026-10-15，实施前）
首轮正式构建在package/install失败，随后一次OPENWRT_VERBOSE=s限定复验220.3秒得到确切错误：aitvbox-suite与aitvbox-mimi-update均声明/etc/smartdesktopmimi/product-edition，opkg check_data_file_clashes拒绝。不是内核/网络/凭据错误，尚未到pack。前序主机包夹具只分别安装两包，没有检查两包文件归属交集，故遗漏了真实安装冲突。原日志和失败结果不覆盖。

修复范围仅公共packaging/aitvbox-suite/Makefile：存在工厂baseline时，由更新器包携带已封存标记，suite先验证baseline及明确public/private选择，不再产生第二份；无baseline时仍由suite生成原标记。suite包release从13增到14避免旧IPK缓存。更新器/IPC/身份/签名格式和已签共用rc24不变，不重编/重签应用。

验收先在原Makefile上用真实A133公共/私有程序夹具执行两个生产安装recipe，比较全部非目录文件交集，应能复现重复标记；修复后两版交集为空，标记唯一归属updater且444、其字节等于封存baseline；无baseline旧模式标记仍有；已有混版提前拒绝不退化。再执行受影响包/基线测试和源码审计。正式rootfs/pack须重新通过，不把独立recipe通过称真实opkg完成。现有失败目录保留，新重试输出另建r2。

不变量：不能使用opkg强制覆盖、取消产品标记/验证、篡改baseline seal或任意换钥；不写服务器17、不动设备/HDMI；应用rc24来自实际bedf1a8，后续系统包装提交可独立变化且准确记录。新运行库/rootfs和完整来源、远端附件仍按原门禁。

修复主机结果：原recipe公共/私有两个真实程序夹具均复现product-edition交集失败；修复后组合payload交集为空、updater唯一标记444且seal不变、无baseline原路径和损坏seal/混版拒绝通过。受影响product_edition/baseline/system_edition共24项0skip通过；完整SDK真实opkg/镜像仍待重跑。首次夹具umask产生可组写文件及首次测试模块搜索路径不完整的调用失败保留，分别修正夹具权限和测试PYTHONPATH后才得最终结果，不以这些工具调用失败解释产品根因。

### SDK 包装变换来源锁修正（2026-10-15，实施前）

r2已经通过真实opkg文件归属和封存基线检查，但最终rootfs后端与来源锁普通strip派生值相差14字节。真实SDK的固定rstrip.sh先用patchelf移除不允许的RPATH再strip --strip-all；副本精确重放与实际rootfs完整相等。旧失败和BUILD-INCOMPLETE保留，不能白名单实测SHA或取消检查。

本批仅修scripts/lock_product_sources.py及受影响测试：新生成schema2来源锁固定SDK rstrip脚本、patchelf和strip的路径/解析路径/SHA及顺序；旧schema1只按原格式兼容读取，不升级历史证据。输入验证→原始ELF副本→固定真实SDK脚本→独立严格退出码重放→全字节比较→前后输入重验→锁记录；任一缺失、变更、执行失败或结果不一致都拒绝。不能因为SDK脚本末尾true掩盖内部错误就接受。

不变量：不修改原程序/SDK脚本/rootfs，不更改现有可信钥、签名rc24或两基线，不重签/改应用版本；公共/私有组成、文件模式、秘密排除和来源校验继续强制。只对必要产品组合重跑正式构建，不挪用失败r2为完成。没有服务器17写入授权，不触碰服务器/设备。

验收：not_run，schema2缺项/篡改/未知顺序拒绝与旧schema1保持可读；真实A133公共/私有程序包装、r2实物完整字节比较和非法输入拒绝；受影响产品锁/来源/系统测试及公开审计；最后正式新镜像/SWU/运行库回读仍not_run。证据统一output/system-product-20261015，下一步先实现并通过受影响主机检查，再建立独立r3构建目录。

本批主机结果：63项受影响测试0skip通过，含真实A133公共/私有后端及私有Tuya三个程序包装、public与r2 rootfs哈希/长度一致、原程序未变；历史两版schema1实锁仍可读。schema2工具/顺序/解析路径/缺项/布尔伪schema拒绝，两版系统构建元数据重哈希仍不能绕过工具篡改；损坏或另指向工具拒绝；SDK脚本伪成功空操作及strip失败故障注入被拒绝。公开源码审计0发现。实际证据package-transform-tests-r2.log和package-transform-public-audit.json。尚未生成新提交实锁或新完整镜像，这些保持not_run，不能把r2改标通过。


## 2026-10-21 A133 实际启动修复与打包门禁（SYSTEM070 / SYSTEM040 / UI075）
用户最新要求已恢复当前板的 FEL、启动演示及发布前验证权限，替代本记录旧的实机暂缓范围。新 SDK 环境重建未完成。

本批修复前记录位于私有交付目录 a133-boot-repair-20261021/prerecord.json 和 source-fix-prerecord.json；既已发生的启动修复明确补记，不追认成事前记录。原 rc6 FEL 写入与校验成功，但首次启动格式化 UDISK/rootfs_data，数据保留验收 failed，旧记录保留。恢复前先匹配硬件和 private 分区，只恢复历史原登记/凭据/WiFi，未重新绑定或换信任钥。

根因证据：SDK 源恢复脚本在 umask 077 下执行 Git checkout，已跟踪 loader/libc/NSS 变成 0700，libgcc 和账号文件变成 0600。板上修复这些权限后 ubus 和正常启动通过，shadow 仍私有。另外，更新器包先安装当前 build/mimi-update，再复制 sealed baseline；旧 baseline 的更新器可覆盖新构建。现在在构建预检和真实 package recipe 校验两者字节相等，不一致必须重新准备基线，不能重封旧 seal 掩盖差异。

流程：正式 rootfs 完整性验证 → 读取原始 ext/SquashFS 权限与 root 所有者 → 检查非 root 可访问 loader/运行库/账号数据库与 shadow 保密 → 任一失败拒绝 pack。检查全程只读，不自动 chmod 镜像，不替代真实启动。SDK 输入仅恢复七项已确认 Git 文件模式，字节不变；缓存/新镜像未重建。来源修复和实机临时修复分开记录。

实现：tools/update/runtime_permissions.py、scripts/build_firmware.sh、tools/update/baseline.py、packaging/aitvbox-mimi-update/Makefile，以及对应 runtime_permissions/baseline/product_edition 测试。不变量：不跳过签名/兼容/硬件身份校验，不公开私有组件或秘密，不修改旧镜像或标签，HDMI 默认关闭。

验收：12 项实际文件系统权限检查通过；受影响集合 41 项通过，1 项初次跳过，随后以已构建的当前 A133 public/private 程序补跑真实安装 recipe 通过。原 rc6 镜像只读拒绝且哈希未变。首次夹具缺嵌入 updater、过旧 provider 输入、缺旧锁负例的失败日志全部保留，最终修复夹具后通过。

当前板为现场修复的 rc6 系统，签名 rc28 应用真实安装、健康提交、普通重启自动进入新版通过；首页/无线网络/返回/涂鸦智家为真实板坐标回放，不能记人工物理触摸。重启后六项静态恢复状态哈希一致，Tuya 运行数据库变化待语义核查。持续观察尚在运行；回退、事务断电、完整功能/数据、新系统候选和最终发布未验收，不称 stable。详见 docs/validation/a133-boot-repair-20261021.json。下一步先完成新系统候选输入闭合，再进行真实回退/断电与完整功能矩阵，保留可启动演示基线。


## 2026-10-23 rc31 / rc8 candidate preparation (SYSTEM070)

Prepare a new local application 1.0.8-rc.31 and system 1.0.0-rc.8 candidate for the integrated boot and capture/driver fixes. Existing signed rc30 admits only rc6/rc7 and remains unchanged. Fresh source build, a new signed compatibility manifest, sealed baseline and normal system/bootchain checks are required. Original trust, hardware identity, data schema and public/private boundaries are preserved. No application behavior change in this version increment. All new build, physical boot, HDMI stability and upgrade/rollback acceptance remain not_run; no stable release or old-result reassignment. Private batch evidence: a133-boot-repair-20261021/system-rc8-r1.
