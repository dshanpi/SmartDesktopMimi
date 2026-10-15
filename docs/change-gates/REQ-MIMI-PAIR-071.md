# REQ-MIMI-PAIR-071 客户端实施批次（事前）

关联后台补丁83e7de7和PAIR020。用户允许软件持续实施，当前不部署后台、不重置设备身份/绑定、不安装A133候选。冻结rc24/rc3/rc4草稿及其56附件，本批新源码不倒填旧产物验收。

流程：现有GET_STATUS → 保持V1 cloud_status_t及已有topic/command编号 → 可选新增末尾配对状态topic，固定schema/长度 → 明确bound显示已绑定、unbound合法未过期码才展示、缺元数据归属未知 → 服务器有效期差值扣除请求耗时并用单调时钟递减 → 同码轮询不延长本地倒计时。旧Provider不发扩展则使用已有V1兼容投影，不能猜账号归属。公共profile依旧unavailable，私有HTTP解析仅私有仓库。

失败：严格拒绝字符串布尔/错误设备/非整数或过期时间/异常或重复响应/非法码；空码与MQTT在线不推断已绑定；错误不泄露HTTP正文、码、长期secret或账号信息。刷新仅用户动作，不由状态查询/页面重开产生新码。

实施位置：公共apps/lv_port_linux/src/system/cloud_pairing_status.h、apps/pairing/pairing_app.c、middleware/middleware.h；私有adapters/100ask的独立严格响应解析与运行时接入；必要SDK provider组合/来源锁另显式更新，不能悄悄改变冻结edition-v2配方。先单元/实际UI编译和独立解析测试，随后实际双profile完整provider构建、来源锁/公共禁用检查。未走完完整链只标部分实现，不称设备修复。

验收：本批实施/主机/A133编译not_run；真实绑定/账号归属/线上部署not_run；设备仍黑屏，不抢串口/重复断电。每批记录实际源码与失败历史，关键源/证据小增量备份。


## 客户端/Provider 实际结果（2026-10-15）
公开可选扩展schema1为32字节，TOPIC_CLOUD_PAIRING_STATUS=22只在旧LED21之后追加；V1 cloud_status_t=276、cloud_cmd_t=4、token/error偏移80/72以及原topic19/20/21、command0/1/2保持不变。旧Provider没有扩展时走V1兼容投影，不推断归属。已绑定状态隐藏码/刷新/绑定步骤；状态未知不声称后台故障或已绑定。相同码轮询只能缩短有效期，不重复续时；单调时钟不受NTP改变影响。

私有HTTP解析和运行时接通；严格检查success/deviceId/bound/Unix整数/六位码/最大600秒，减去完整请求耗时并保守取整；旧响应缺元数据保持UNKNOWN，不硬编码一小时，不输出响应正文、码或长期secret。通用结构和UI公开，专有解析仅在SmartDesktopMimi-Private，公共profile仍unavailable。

passed：私有ASan/UBSan解析与时钟/重放测试；受影响主机63项中60通过、3可选SDK/外部项skip；实际A133公开30与私有32编译单元完整编译/链接并做真实依赖闭包审计，两次来源参考与实际产品组合输出一致；4292文件标准overlay源校验/应用/读回，95个manifest组件身份核对；overlay15、私有6测试通过；实际LVGL四状态×双语言×明暗共16渲染及断言通过，三张中文实图人工复核无裁切。新ABI还有主机静态断言，A133 UI仅实际受影响编译单元通过，不能写整包链接通过。

保留失败：stage r1因公共stub父目录已存在而失败，修复生成器后按原recipe/可执行SHA复用已通过公开参考，不重复构建、不抹掉失败。旧edition-v2目录/原工具不动，edition-v3只新增公开契约和配方，私有body不复制进SDK新增目录。旧rc24/rc3/rc4包/标签和56草稿附件不动，新源码验收不倒填旧包。

not_run：后台线上部署（补丁83e7de7由用户后续合并）、真实账号/新绑定、全应用新包、设备安装和真机配对；既有板仍黑屏。主机与源码同步不能称设备配对修复。证据docs/validation/pairing-client-071.json及开发证据output/pairing-client-20261015；接续固定实际三仓提交、远端源码读回/CI及关键增量备份，再按新环境计划实测。


### IPC门禁拦截与兼容审查
首次check_contracts主动拦截middleware头SHA变化（contracts-r1.log保留），没有跳过门禁。复核旧源码dispatcher对topic>=TOPIC_MAX直接返回而不写数组/断连；V1结构原字节、命令编号不变，新增仅topic22/32字节schema1；实际旧V1与新可选状态UI fixtures及ABI断言通过。显式锁更新mimi-ipc-v1.2，保留最小兼容mimi-ipc-v1.1/base Ports，新增头纳入SHA锁。真实旧UI/新Provider双进程及实体混版仍not_run，不把源码审查写成实机通过。未重建不受影响后端/旧产物。


### 远端UI清单门禁失败与修复
原627e1ef的Application update gates（38730596624）在UI coverage inventory失败，原因新配对页源SHA未同步ui-catalog，非已通过主机项或设备状态改变。先比较原/现清单，变化仅pairing_app.c，真实16变体与页面断言已经覆盖；现补authoritative-bound、unknown-ownership、reject-optional-schema、same-code-poll-no-renewal四场景，再显式record清单。清单只绑定源码/场景，实体四项保持not_run；保留远端失败，不改旧包、不重编无关代码。私有f80bbfb的CI已success，SDK实际默认master已快进fa0d799，初次同步额外main同值引用保留未删。
