# REQ-MIMI-LOGIN-052 Ubuntu 正式图形登录启动

前序：SESSION026 / ACTIVATE029 / DEBACTIVATE030；MIMI-UPDATE-001、TRACE001。2026-10-15实施前记录。
用户已授权开发主机 Ubuntu 桌面安装、图形登录启动及数据验收。当前只有 GDM greeter，先前 Xvfb 结果不能代替普通用户图形登录。

## 范围与流程
平台提供独立 root 管理登录桥接服务，只面向受控登记的固定 UID/GID。通过真实 logind 选择 seat0、active、local、class=user、type=x11 的唯一会话；SSH、greeter、远程、Wayland、歧义均不能触发桌面。暂不声明 Wayland 支持。

```mermaid
flowchart TD
 A[加载 root 固定平台配置及已钉住更新器] --> B[只读 logind]
 B --> C{唯一指定用户本地图形会话}
 C -->|无| D[等待 不启动]
 C -->|歧义或不支持| E[真实失败阶段 不换其他用户]
 C -->|有| F[安全读取该用户固定 Xauthority 来源]
 F --> G[在同一更新互斥锁内写入短期显示授权与会话配置]
 G --> H[现有 start-selected 验签和真实平台登记后启动]
 H --> I[非root桌面与Provider 绑定登录scope]
 I --> J{退出登录或停止服务}
 J -->|是| K[只停止本配置单元 清理临时授权]
 H -->|失败| L[本次登录不无限重启 保留失败]
```

## 回归不变量
- .deb仍仅搬运签名应用载荷，不增加 maintainer scripts，不修改信任/身份/硬件/Provider；登录桥接是平台安装内容，不能混入应用包。
- 不修改GDM自动登录、密码、账号绑定、用户主桌面配置或其他会话；不读取/记录 Xauthority 正文，不把SSH/Xvfb/greeter当图形登录通过。
- 只采用根控固定配置；允许字段严格校验，动态输入仅来自 logind/固定用户授权文件。Xauthority链接/硬链接/权限/owner变化拒绝；不使用任意调用方路径。
- 模式变更与应用/系统更新共享事务锁；start-selected保持原验签/登记/恢复语义。登录会话消失时systemd scope绑定结束桌面，不无穷重启。
- A133路径不受影响；原 rc23/sysrc2候选不因独立Ubuntu平台工具变化重建，可信密钥不变。

## 实现位置（计划）
services/mimi-ubuntu-provider/login.py；packaging/mimi-ubuntu-login/mimi-ubuntu-login.service；services/mimi-update/update/ubuntu_session.go及测试；tests/sdkbuild/test_ubuntu_login.py；Provider README；本记录/索引/进度/批次。

## 验收条件与当前结果
实现、边界回归与原Ubuntu启动回归：not_run（待实现）。实际平台systemd安装/无普通图形登录时等待、拒绝greeter/SSH：not_run。真实普通用户图形登录、退出/再次登录、升级后的启动及完整用户数据：not_run，需实际登录会话。不得用虚拟夹具提升为真机 passed。

## 输入、版本与接续
基准 source fbcba42f19655b32b8bcfd294de2f60f4f404a2b，Ubuntu20.04/x86_64已登记；当前签名rc22包616065f1acfe1f75f0672e9502056de5e2b88dc5b728a81876b0242c8cc3d06e。本轮仅独立平台工具，不发新应用号、不上传。证据保存 output/ubuntu-login-20261015，关键增量进入 .delivery。
ADBbinding尚缺，A133主线保留；可独立实施Ubuntu。外部云设备/语音/账号和全部发布门禁继续保留。

## 实现与第一次回归（2026-10-15）
独立登录桥接与 systemd 模板已实现；Go UbuntuSession 增加可选 login_session，旧显式显示配置保持兼容，新增入口独立检查 logind 并 BindsTo/After 对应登录 scope。桥接对短期配置写入/停止使用共享更新锁，启动仍由 start-selected 重新验签；不混入应用包。
Python边界第一次 failed：测试夹具在group-writable开发目录内，祖先检查真实拒绝；未放宽守卫，改为安全用户home临时fixture，第二次13项（含旧会话6项）passed。Go Ubuntu/StartSelected scope及事务锁回归passed。源码尚未正式平台安装；真实登录/数据/升级仍not_run。日志 output/ubuntu-login-20261015，失败日志保留。

## 真实CLI兼容缺陷（实现后实际读取发现，保留失败）
首次部署的无启动/数据保持检查实际通过，但登录属性读取不合格：Ubuntu20.04 loginctl 对单个 `--property=Id,User,...` 返回0却空正文，不能把该输出当真实过滤验收。必须修复为逐个 `--property` 并拒绝缺失属性，新增真实本机登录属性读取验收；原部署记录保留，仅证明服务未启动桌面，不证明过滤正确。A133/应用与签名密钥不变。

修复已完成：逐字段property参数且缺字段/会话Id改变拒绝。真实c1属性已读出 User125/Classgreeter/Typewayland/seat0，真实固定UID1000会话筛选None；14项Python边界passed。CLI缺陷不改变原服务确实未启动UI/数据未改事实，但过滤验收必须以后续真实完整属性证据为准。

## 2026-10-15 平台部署及真实读回
最终实现 source64253e65fb3e41cd398f504c8bc2007dc6404c41。native amd64更新器d53ce90a4d8865290b515295e2b2238427efa06cf36803bf2aa66a320d809798、CGO0ARM64更新器8cb1c02a51ef723d7b85ff8068fa2414255d838f12eb54a5fcc69ea81960e218实际编译passed，未替换A133板或冻结系统产物。
独立root平台/固定UID1000安装完成；mimi-ubuntu-login.service enabled/active，读取11条真实logind投影，无普通用户X11，c1为UID125/greeter/wayland。正确筛选等待passed，桌面单元not-found，未创建临时授权/运行配置。真实Go平台启动入口也实际拒绝c1，未改更新状态；不把该拒绝当登录成功。
第一次平台安装无启动和数据保留是真实，但property空投影失败仍保留。r2部署检查后写证据时把自己导入产生的__pycache__当普通文件，序列化failed；未重复安装，r3独立按普通文件核对root实际程序哈希和配置pin、真实daemon/完整logind属性passed。该缓存不是应用payload或来源输入。
已将之前验签和激活通过的rc22 deb正式保留安装，不再测试后purge：dpkg版本1.0.8~rc.22-1/deb9daeff49a71056b3b1f44d7b9407a85e7a26a50bbfe3d1573b542005e692b711、签名应用包616065f1acfe1f75f0672e9502056de5e2b88dc5b728a81876b0242c8cc3d06e；实际安装载荷与原签名包逐文件相同，verify0，原平台9项hash/更新state/设置1文件均未变。只证明这一范围，不声明完整用户数据通过。
14项Python（含旧会话）边界passed；全Go测试在8b91版本passed、642实际CLI修复后仅重跑受影响Ubuntu/StartSelected passed，旧结果不移记新版本。
证据 output/ubuntu-login-20261015/{build-result-r2,deployment-result-r3,actual-greeter-launch-refusal,retained-deb-result}.json及回归日志。真实图形登录/退出/再次登录、登录后健康和升级、完整媒体及扩展数据仍not_run；已一次请用户在VM控制台登录ubuntu并选择Ubuntu on Xorg，不请求密码，不开启GDM自动登录。普通Ubuntu Wayland桌面未支持/未宣称通过。
A133 MCP可读scope，无活动hardware任务，device4保存ADBbinding仍空；该维护输入pending，已提问不重复。原FESfailed/失联未解决/全部升级回退断电数据与功能门禁继续保留，无公开上传。下一步获得实际X11会话后核对启动/健康/退出；获得ADBbinding后完成原key认证、传包、app23及sysrc2真机矩阵。
