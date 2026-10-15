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

修复主机结果：原recipe公共/私有两个真实程序夹具均复现product-edition交集失败；修复后组合payload交集为空、updater唯一标记444且seal不变、无baseline原路径和损坏seal/混版拒绝通过。受影响product_edition/baseline/system_edition共23项0skip通过；完整SDK真实opkg/镜像仍待重跑。首次夹具umask产生可组写文件及首次测试模块搜索路径不完整的调用失败保留，分别修正夹具权限和测试PYTHONPATH后才得最终结果，不以这些工具调用失败解释产品根因。
