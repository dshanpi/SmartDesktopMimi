# REQ-MIMI-SESSION-026 Ubuntu 独立桌面会话与真实健康

2026-10-14 实施前记录，承接SDK025和UBUNTU024；MIMI-UPDATE-001/TRACE-001继续。基准eaf24995bb1db6bec50a621e96af2a0fc31219f0。

## 背景与边界
实际Ubuntu SDK已经构建完整应用，但现有启动和健康检查固定Tina路径。先实现普通桌面用户的独立会话生命周期，提供用户私有运行目录中的真实UI心跳、Provider握手和进程监控，作为后续可信平台安装适配的前置。不是安装器或签名绕过入口，不宣称root平台登记/签名deb已完成。

## 流程
验证用户/私有路径及应用构建记录→取得会话单实例锁→启动独立Provider→握手确认→以独立配置启动指定应用→等待真实UI心跳/后台状态连续前进→标记ready并持续检查。任一子进程退出或心跳超时→停止自有子进程→清理自有运行文件；不自动重启循环，不删除应用版本或持久配置。正常关闭桌面则结束会话。外部可读健康命令必须验证实际PID/exe和新鲜度。

## 不变量
不读取凭据、不修改主机网络/驱动/密钥；仅自有PID，不按名称杀进程。运行目录0700/同UID，配置持久目录与运行/应用目录分离。Ubuntu健康路径受限私有目录，A133保留原固定路径和协议。Provider保持独立，普通应用包不得包含服务或可信配置。界面外观/业务不变，仅健康落点适配；改应用产物后重新构建，不借旧验收。

## 实现与验收
计划位置services/mimi-ubuntu-provider/session.py、apps/lv_port_linux/src/ui/desktop_health.c、CMake原生目标定义、源码归属和相关测试/说明。需要新本地候选rc19（不上传），实际源码/hash稍后补。
- not_run：真实SDL+Provider会话启动、健康三次前进、单实例、正常关闭、Provider退出、UI停滞拒绝、配置保留和清理。
- not_run：私有路径/链接/版本记录/目标错误拒绝。
- not_run：新产物Ubuntu/A133构建及受影响回归。
- not_run：root可信平台注册/签名deb激活/真实升级回退与数据矩阵。
- blocked：A133长稳/系统验收仍未完成，SDK025恢复不改变此状态。

## 接续
证据output/ubuntu-session-20261014/。先完成会话和健康实际运行，再由平台可信安装适配接入更新器；避免将开发会话启动称为签名安装验收。用户可选语音默认策略、首页确认样式和编译配置仅主机的要求保持。

边界补充：本轮普通用户会话退出/子进程故障可清理；supervisor自身SIGKILL的子进程收容仍需生产服务管理器，不能将该场景标已支持。平台可信适配接入更新器前必须补齐。

继续补齐强制退出边界（实现前）：Linux PR_SET_PDEATHSIG使supervisor被强制终止时其两个直接子进程一并终止，校验父PID避免启动竞争；复启仍需私有锁和过期端点核验。将用真实进程强杀验收，数据原子性/平台事务断电不因此通过。

提交前：6项会话边界测试及SDK/服务整组63项passed，74项架构passed，冻结V1/源码归属和公开候选审计passed。实际会话生命周期测试需rc19固定构建后执行，当前not_run。新增测试脚本tests/sdkbuild/ubuntu_session_smoke.py会运行实际SDL与Provider，检查进程/心跳前进、关闭、失效/停滞和父进程强杀恢复；不模拟成功状态。

## 2026-10-14 实际固定构建及会话验收
源码e5b7186529ec574314a0de5e8e21b49b301d7456，本地rc19。Ubuntu完整桌面/Web/分发树及构建后输入复查passed；桌面SHA256 b84fd65f87bade14f82984444b8a5f6408f70d4f6e6e6a514d42dedf859dfe3a。SDK锁d92c1a17ea83af45f3ca6f04678b6d893237d74873f8e76430dec251519d1740。A133回归编译passed，桌面SHA256 1abae4491a05b15245cfe483aa6461462894ef69cfb59273d9b394ba6a32a269，与前序字节相同；未安装新候选。

独立Provider源码3a258ea77306b8d55eb693d86ddd829cc5c63923，SHA256 dcc7e62d521645e9d8a7519bea66a557d8a167f92c30e5477ceb66687ae9329c，并非rc19重编产物。实际Ubuntu20.04/x86_64主机+隔离Xvfb，五组passed：真实UI/后台心跳前进、重复启动拒绝及窗口正常关闭；复启配置字节保留/SIGTERM清理；Provider退出导致会话失败并结束UI；SIGSTOP界面事件循环导致健康拒绝并清理；supervisor SIGKILL终止两个直接子进程且复启恢复。截图人工核对passed：首页内嵌图标和短横线分页保持，Ubuntu HDMI显示不支持。不是实体屏幕/实体断电/签名安装验收。

证据output/ubuntu-session-20261014/result.json、lifecycle/result.json和session-home.png、构建/测试日志。初始not_run项由本次实际结果补充，不覆盖历史。可信平台登记及更新器接入、签名deb安装升级回退/数据、A133系统验收仍not_run或blocked；无上传。下一步接入Ubuntu平台可信身份和签名更新事务，保持应用包不携带Provider/可信密钥。
