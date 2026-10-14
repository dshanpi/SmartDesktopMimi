# REQ-MIMI-UI-018 缺失 HDMI 能力时的真实界面

记录日期：2026-10-14；前序：REQ-MIMI-BUILD-017、REQ-MIMI-UI-016；适用 MIMI-TRACE-001 / MIMI-UPDATE-001。状态：实施前记录。

## 背景与范围
Ubuntu 原生窗口首次验证发现没有 HDMI Provider，却显示 Ready、默认打开的直通开关。修复开发主机不支持 HDMI 时的首页、顶栏/控制中心、HDMI 应用入口和控制请求；保留用户批准的图标、内嵌分页和短横线。应用构建能力上限不代表服务在线，也不代表平台验收通过。A133 已有协议/驱动不在本次修改范围；A133 断线后的动态能力探测另行验证。

```mermaid
flowchart TD
 A[构建确定应用能力上限] --> B{支持 HDMI 接入}
 B -->|否| C[首页显示未支持 开关关闭且禁用]
 C --> D[点击 HDMI 应用显示原因及返回入口]
 C --> E[拒绝控制 不改变保存设置]
 B -->|是| F[保留现有平台交互路径]
 F --> G[真实 Provider 验收 独立记录]
```

## 回归不变量
- 未接入能力不能因历史设置、点击、状态帧变成已启用；不能连接 A133 固定控制套接字。
- 禁用分支不写用户设置、不发送 HDMI 开关命令、不启动采集；设置原值保留。
- 不改冻结 IPC、平台服务、设备身份、签名密钥；不引入桌面编译配置页。
- 构建能力为支持不等于在线；本次不宣称解决 A133 静态就绪语义或整机失联。

## 实现位置与验收计划
待修改 apps/lv_port_linux/CMakeLists.txt、src/ui/desktop_capabilities.h、desktop_clock.c、desktop_status_bar.c、ui_i18n.*、src/apps/hdmi_mcp/hdmi_mcp_app.c；tests/sdkbuild/ 的实际 SDL 窗口/输入测试。

| 检查 | 当前结果 |
| --- | --- |
| Ubuntu 首页未支持、开关关闭禁用 | not_run |
| 点击开关/HDMI 入口无控制命令、返回正常 | not_run |
| 多分辨率窗口、关闭、拒绝未编译分辨率 | not_run |
| 主机回归、A133 交叉编译与冻结边界 | not_run |
| A133 新产物实机回归 | not_run |
| Ubuntu 真实 Provider、deb 安装升级 | not_run |

实施基准 0535803ceeaabdce6929a86d2c5ff0d51d294b23，测试目标 Ubuntu 20.04.4 x86_64；证据写 output/ubuntu-hdmi-capability-20261014/，提交/二进制哈希在构建后追加。本轮只改工作源码、不安装板端；关键增量源码和证据归档到 .delivery/critical-source-20261014/。既有失败证据保持，不上传应用/Release。

接续：先修正并验证状态，再推进 Ubuntu 真实服务及 deb；A133 稳定性、系统升级/回退/断电/数据验收仍未完成。

### 开发中检查 1
主机架构 50 项 passed；初次新增渲染测试编译 failed：测试未声明公开开关对象，已补 extern，待重跑。初次编译期间仍有源码修订，不能作为最终产物证据。原始记录 output/ubuntu-hdmi-capability-20261014/renders-no-hdmi/build.log 保留。渲染工具 scripts/render_ui_review.py 新增隔离的无 HDMI 构建目录，避免能力编译标志污染既有渲染缓存。
