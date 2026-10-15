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

2026-10-14 DEBACTIVATE030进行中通道复查已空闲；仍原project4/device4/COM17/USB绑定，已有串口handle为closed。本轮继续同门禁只读版本/健康/HDMI/系统状态核对；不刷写或安装。若再次无响应，记录失败后沿用既有授权、再次核对作用域空闲，受控30秒恢复并关闭自有句柄。

030实际：两次串口空和ping失败；首次恢复前组合断言失败未调用电源，随后重新逐字段核对全部通过，执行授权30秒cycle。首次命令被getty消费未执行，后实际确认rc17/previousrc16/trialfalse/highwater17/health0、p7错误0、46.056℃、1512000kHz；无频率/驱动改动。启动存在rootfs unchecked和SDIO -22告警，随后DHCP成功；不推定根因。无pstore，无打开video句柄。serial-3关闭，无SSH授权/新安装。output/ubuntu-deb-activation-20261014/a133-result.json；boot-private仅私有保存，归档用脱敏摘要。汇总脚本首次断言固定日志段失败，确认health0实际在后续diagnostic2读回而非diagnostic1，原日志保留。恢复不是事务断电验收。

## 2026-10-15 先有开机画面、Linux后黑屏（只读接续）
用户确认实体屏先显示开机画面，随后黑屏。本批无新硬件操作，不重复继电器或安装镜像；原临时救援参数/无线链接配置已完整恢复，自己串口已关闭。旧rc17的framebuffer能成像不能代替实体屏。

源码和保存日志交叉核对：U-Boot实际日志LCD open finish；Linux要求lcd_driver_name=d320fpc2403，但逐项登记的panel_name列表没有同名项。驱动注册函数仅在名称完全相同时填入cfg_open_flow/close_flow；现SDK面板表的d320入口受CONFIG_LCD_SUPPORT_D320FPC2403_MIPI控制。当前候选构建配置为y，先前SDK047编译/打包检查已核验面板对象；这只是已存在修正输入核对，不新建或安装候选。旧系统缺少匹配登记与Logo后黑屏相符，是受支持的显示路径推断，尚非实体修复对照或eMMC/无线失联唯一根因证明。两类故障分别保留，禁止把新镜像已有修正写成设备已修好。

证据：保存的normal-original-restored启动日志（原始私有）、SDK047 sdk-panel-readback及kernel-gate结果、SYSTEM070既有镜像检查；新公开记录仅脱敏现象/字段/结论，不复制SDK源码。下一步需按原用户延期要求和内核/启动链门禁进行受控新系统实测；完整实机not_run。当前只读分析passed、实体屏恢复failed、根因完整闭环not_run。

用户随后报告手动电源后一度恢复正常，又自行画面消失；不能把上述面板缺登记解释为唯一原因或每次必然失败。只读现场保留：启动约64秒在保存WiFi关联请求后日志停止，此前触摸I2C连续失败，随后串口无新字节、网络无响应。没有本轮新电源/配置/采集操作；无已确认Linux提示符时没有乱发Linux诊断。板载电压采样最低4.14V只作待核实的线索，采样不能独自证明供电是根因。已请用户在保持额定规格下可选换短线做单因素对照，软件工作继续，不把它作为开发前置。旧原版仍未稳定修复。

本批文档检查：所有更新页面的相对链接/图片落点已实际检查（123项），公开56附件元数据与原远端复验结果逐字节一致；改动仅Markdown/JSON，无产品重建。首次链接检查发现README中2个旧失效路径，已更正到实际实现/平台边界文档；r1 failed及r2 passed均保留在开发证据。设备修复未通过、整体未完成；提交覆盖和远端源读回待实际执行。
