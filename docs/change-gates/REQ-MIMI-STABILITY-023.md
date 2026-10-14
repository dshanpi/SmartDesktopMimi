# REQ-MIMI-STABILITY-023 A133 空闲失联记录与受控恢复

2026-10-14 本次恢复前记录；关联MIMI-UPDATE-001、用户既有MCP继电器恢复授权和此前传输/空闲失联证据。前序output/home-icons-20261014/result.json、output/transfer-cpu-diagnostic-20261014/result.json。不能用恢复成功替代根因修复或长稳验收。

## 背景与范围
上轮rc17受控配对复验后保留桌面运行，关闭临时SSH与串口，HDMI未开启。本轮只进行Ubuntu主机开发/构建；A133只读检查时串口两条命令均无输出、ping超时，未进行新产物传输/安装。最后确认版本为rc17/源fe58653/包34fc809a…beed8，失联时无法再次确认版本，不能把预期当实测。

## 流程与失败分支
先记录原始空响应和网络失败→核对MCP保存的projectId4/deviceId4、COM17、绑定USB和通道空闲→使用既有授权仅对该目标继电器断电30秒恢复→读取真实启动、当前/previous/trial状态、健康退出码和文件系统错误→关闭自有串口。若通道占用/绑定改变则blocked不操作；未恢复则failed并继续主机独立工作，不绕过烧录/模式切换限制。

## 回归不变量
不刷机、不换可信钥、不改身份/云绑定、不修改频率/电源/驱动配置；仅已授权电源恢复，不将断电伪装为升级事务断电验收。保留当前及previous应用和既有关键备份；MCP只操作已保存A133作用域；HDMI默认关闭。日志只保存启动/脱敏状态，秘密和消息正文不进入记录。

## 验收条件及证据
failed：串口空响应与网络超时，output/ubuntu-provider-20261014/a133-existing-*.log及健康JSON；version_confirmed=false。not_run：恢复后的实际版本、health退出码和ext4错误；后续追加。根因与长稳验收仍failed/未解决，不能归因于电源、CPU、网络传输或Ubuntu新服务。

## 接续
比较空闲与传输场景，保留启动/恢复证据；系统更新回退、事务断电、数据和长稳仍为发布前置。Ubuntu平台代码与该故障独立，不安装到A133。只备份小型证据；新应用/Release仍不上传。

### 恢复实际结果
MCP核对保存project4/device4/COM17与绑定USB，通道空闲后执行cycle/offTimeMs30000，回执state=on。BOOT0/U-Boot/Linux/ext4启动片段保留。第一次命令在getty尚未就绪时被消费，没有MIMI_RECOVERED标记，明确not_run；出现root提示后重新执行只读检查，确认current rc17/包34fc809a…beed8、previous rc16、trial=false。健康命令真实退出0，ext4 p7错误0，恢复passed；serial-9关闭，未开临时SSH。证据recovery-status-r2.log、recovery-health.log、recovery-result.json。没有新包传输/安装，根因和长稳继续未通过，不将本次电源恢复记为升级事务断电验收。

2026-10-14 SDK025构建期间复查：两条UART命令空响应、ping192.168.1.46失败，无新设备传输/安装。已核对project4/device4/COM17/USB固定绑定，硬件通道空闲。原故障failed，版本未确认；沿用已有授权执行30秒继电器恢复，后读版本/health/内存/系统日志。此次不是升级断电验收，不改变密钥/身份/驱动/频率。

SDK025本次恢复结果：rc17/previousrc16/trialfalse、health0和ext4p7错误0已实际核对，serial-1关闭，未改身份密钥/HDMI/频率或安装候选。output/ubuntu-sdk-20261014/a133-recovery-result.json；初始失联failed、重启恢复passed、根因未解决。

2026-10-14 ACTIVATE029结束后只读核对：LYNX仍project4/device4/COM17/原USB绑定，但hardwareSafety.busy=true，活动为另一COM18串口。按本门禁不打开串口、不恢复电源、不传输；本轮A133实测blocked，版本仍仅沿用最后rc17记录。继续Ubuntu独立deb链路，不能宣称本轮A133健康。
