# REQ-MIMI-UBUNTUAPPS-076 Ubuntu扩展应用与数据消费者

2026-10-19实施前记录。承接UBUNTU022、SDK025、DEBACTIVATE030、LOGIN052、MIMI-UPDATE-001，基准e569ba05d0aac2a5af1da6c271c043932a5ae89a。用户已恢复原Ubuntu开发机软件工作；A133、新SDK物理验收仍延期。

## 完整目标与边界
Ubuntu共享桌面应消费平台安装的扩展应用，签名安装/发行者归属、明确的架构/运行时准入、受限制执行、真实storage.app写入/重启读取以及桌面升级/回退后的应用和数据保持必须逐项完成。不能以复制文件、假服务回复或只校验哈希代替消费者验收。旧schema2和安装策略当前仅认AArch64，不能改字段冒充Ubuntu支持，也不直接复制A133 SDK私有服务源码进入公共应用。平台服务独立于桌面更新包；原信任、绑定、rc29、其他应用及用户数据不变。媒体能力仍是UBUNTU022独立未完项。

## 本批：解开共享桌面的固定A133路径
现状：user_app.c固定/overlay/aitvbox/apps和/var/run/aitvbox/apps.sock，Ubuntu并无这两个入口。使用已有平台配置LV_PORT_LINUX_CONFIG新增user_apps.root和user_apps.socket_path；默认维持原A133入口。显式空值禁用相应入口；相对/父目录/控制字符/过长配置拒绝为禁用，不截断、不回退到旧平台。配置属于开发/平台入口，不加入用户桌面设置。目录及socket分别配置；仅有目录不宣称执行服务存在。

流程：平台配置→默认/显式根路径→目录扫描→嵌入UI读入；invoke仍送平台appd验证权限。路径无效或目录/服务不存在→空目录/原失败提示，不启动或安装服务。测试真实C配置解析和真实LVGL读取独立临时目录的声明式页面、选择/关闭/重开；模拟页面不是签名安装验收。

## 验收与接续
实施前not_run：配置正反例、真实LVGL消费、共享完整编译。后续仍not_run：Ubuntu平台安装工具/目标准入、真实sandbox/storage.app服务、签名安装和运行、实际桌面升级/回退数据、真实logout/relogin。必须继续完成这些目标，不能以本阶段完成替代全项。证据output/ubuntu-apps-076-20261019；不得操作生产应用目录，不强制注销用户。

### 归属门禁纠正（实现过程中）
首次将字段加入system/config/app_config.*被source_ownership拒绝（shared-frozen），原失败和差异保留。未放宽锁、未重标SDK。改为src/apps/user/user_app_config.*应用侧独立客户端读取同一平台配置文件，冻结共享三个文件恢复基准原字节；不修改平台IPC配置/ABI/Provider。首次配置边界测试还复现NUL/超长行被截断接受，新增完整物理行读取后拒绝整份扩展入口配置；显式配置文件缺失也禁用两入口。需要按最终层次重新执行受影响检查。

## 本批实际结果
应用侧user_app_config.c/h与实际user_app.c接通；10项真实C配置检查passed。真实LVGL临时声明式目录读取、选择/重开与服务不可用失败检查在三尺寸/双语言/双主题共12图通过，另禁用service端点1组通过；没有使用真实appd，不能叫签名安装或storage验收。小屏布局已人工看图。源归属原锁73491272和IPC1.2通过，冻结文件字节保持。原归属拒绝与配置截断失败历史保留；完整新desktop构建、平台安装/架构准入、sandbox/storage、升级数据、真实登录与物理尚未执行。详见docs/validation/ubuntuapps076-client-20261019.json。


### 完整构建追加结果
干净源码866f7e0fbcc87a45fbec31d263a987be42cbf704使用原Ubuntu SDK锁完成desktop/Web构建，89.24秒退出0；28个暂存文件逐项SHA256通过，编译数据库确认新增客户端读取器实际编入。桌面SHA256 e988a25c20e1e819c270097b4a7c009757e5434e8c3380c58f887d8a63bf33fb。此为未签名、未安装测试候选，未替代rc29；前一阶段not_run记录保留为历史。未来签名必须换新版本重新形成干净源码构建证据。Ubuntu真实扩展安装/运行/storage与媒体完整数据、真实重登录、A133物理验收仍未完成。详见docs/validation/ubuntuapps076-build-20261019.json。


### 远端门禁失败与修复前记录
86c0d2c的Application update gates在UI coverage inventory失败（run39204787948），本地同命令复现。原因是user_app.c改动后遗漏更新已审阅页面指纹；三个回调绑定无变化，目录/端点行为已有最终13组LVGL证据。将补充平台目录、无效配置、禁用服务场景并重新记录inventory；此为覆盖清单修复，不修改应用二进制/平台锁，也不重复完整构建。原失败保留。
