# 下一阶段：通用 AI 桌面需求与设计确认门禁

状态：D02 已获用户于 2026-10-13 批准实施；以下 D01/D02 过程说明为历史记录，当前授权以文末批准条目为准。实际完成情况见开发根目录 output/next-phase-20261013/progress.json。

## 执行顺序与门禁

先完成当前统一发布机制、系统集成与必需真机验收，再推进本阶段。以下五项必须先交 UI 渲染效果图、业务逻辑与功能需求说明，由用户确认后才能开始实现或修改产品代码。当前既有发布/验收工作的授权继续有效；不得把下一阶段功能混入正在验收的 rc 应用或系统镜像。不得以原型、截图或方案通过代替功能实测。

## 用户要求

1. 首页去掉底部横栏导航，避免重复横栏。现有宽应用导航区改成此前抽屉形态，能够滑动选择不同软件。设计稿展示收起、展开、滑动选应用和进入/返回状态，以及不同分辨率的布局。
2. 解决 100ask.net 配对问题，设计开源版本的密钥安全边界。说明设备身份、用户配对/解绑/恢复、服务端授权、公开源码和固件中允许携带的内容；不得把服务端主密钥或可批量冒用的凭据编入开源包。现有私有功能在确认替代流程之前保留。
3. 将涂鸦其他能力纳入产品，尤其物联网设备，做成独立应用。先盘点可接入能力及平台授权/API 前置，再设计设备列表、配网/绑定、状态、设备控制、失败/离线及权限流程。不能在未验证官方能力和账号权限时宣称全部设备可用。
4. 语音转文字后可对屏幕下达控制指令，做成独立可选功能。设计启用/关闭、语音与文字确认、可执行命令范围、目标定位、操作反馈、失败与取消；敏感操作需明确确认，外部页面/语音转写不能自授权限。说明本地/云端识别、数据流与隐私选项。
5. 桌面支持 .deb 包和不同屏幕分辨率规格，编译时自动检查 SDK 依赖，并允许手动选择支持的分辨率列表和单板平台。形成统一 AI 桌面架构与构建流程。应用/Provider 分层和板型、ABI、运行依赖匹配继续有效；Debian 包不能假定可直接安装在当前 OpenWrt/Tina 上，需区分 Debian 系目标与现有 Tina 原生应用包。A133 首轮，T527/RK3576 在拿到 SDK、板卡与证据前仅列待接入。

## 设计交付范围（本节原为待制作清单）

- 首页与应用抽屉、多分辨率适配的 UI 渲染图。
- 配对、涂鸦物联网、可选语音控制和构建选择流程的主要页面、正常/空白/离线/错误状态图。
- 五项功能的业务流程、状态变化、输入输出、依赖与验收标准。
- 密钥/设备身份/用户权限和语音数据安全方案。
- 分层接口、平台与分辨率能力矩阵、SDK 检查及 .deb/Tina 各自打包与更新边界。
- 明确需用户提供的平台/账号/API 授权信息；不要求在聊天中粘贴私钥或密码。

本记录不代表方案已获确认，不代表支持平台或功能已实现。


## D01 首版设计交付（2026-10-13）

已制作五项功能的本地交互方案和业务/权限/依赖/验收说明：开发根目录 output/next-phase-design-d01.html；Windows 当前工作区 design-review/index.html。包含首页抽屉、100ask.net 配对、涂鸦智家、可选语音操作、平台/屏幕/打包配置，以及空白、离线、失败和确认状态。

这是待用户评审的原型，尚未实施产品功能，示例设备、账号和检查结果不是真实联调。HTML 脚本语法检查通过；浏览器工具拒绝 file: 本地页面访问，未完成浏览器交互/布局实测，不标为通过。另提供首页/抽屉概念效果图，不能代替原型或设备验收。用户询问“为何没看到”后补交；先前只有需求清单，不是已经交付的设计。


## D02 用户纠正：编译配置不属于桌面 UI（2026-10-13）

SDK 自动依赖检查、单板平台、支持分辨率列表和打包格式选择，仅属于开发主机命令行编译配置。保存固定配置供交互构建和非交互 CI 共用；设备桌面只按实际屏幕及已编译支持范围适配。D01 效果图中的“构建与安装”桌面面板作废。D02 将该部分改为独立编译流程与配置说明，不新增桌面应用或设置入口。其余四项桌面设计仍待用户确认，新功能尚未实施。

当前预览：output/design-review/index.html。脚本语法通过；浏览器交互及布局检查未执行。


## 2026-10-13 D02 用户批准实施
用户确认修订图并要求当前会话循环完成全部功能和交付。现在授权实施首页抽屉、100ask.net 配对/凭据隔离、独立涂鸦 IoT、可选语音文字控制，以及仅开发主机上的 SDK/板型/分辨率配置和 deb 打包。不得将编译配置加入桌面 UI。保留旧验收候选，新功能建立新提交/产物并重跑受影响验收；既有系统、稳定性和完整发布门禁继续完成，全部必需真机测试通过后才上传。此前待设计确认状态由本次批准更新；不把批准等同于功能或真机验收完成。


### 2026-10-14 responsive desktop implementation
Home now derives its viewport from the actual display. Wide displays retain two cards;
narrow displays use a vertically scrollable column above the persistent drawer grip.
The status bar uses a second clock row on narrow displays. Short displays use a single
row of horizontally paged launcher cards. Control Center cards scroll horizontally.
Developer configuration remains exclusively in tools/sdkbuild; no SDK/build settings
are exposed on the desktop.

Use scripts/render_ui_review.py with --resolutions 1024x768 480x800 800x480 1280x800
1920x1080 to exercise real LVGL rendering and pointer navigation with host fixtures.
This covers Home, launcher and Control Center geometry, not every application screen.
Physical qualification still requires the corresponding display, touch calibration,
application matrix and signed build receipt. Only the actual A133 1024x768 panel has
hardware evidence; selection in a build profile does not qualify other resolutions.

## 2026-10-14 首页标注图纠正
用户明确：删除红框的整条底部导航；保留绿框应用区在首页内，左右滑动选择软件并点击打开，不用一个小抽屉入口代替绿框，不要求跳到全屏应用页。上方助手/HDMI 卡片保留，编译配置不进入桌面。旧抽屉验收不代表本次首页内应用区验收。


## 2026-10-14 首页图标效果图用户确认
用户已确认柔和底色圆角图标方案：图标下方应用名称，首页内横向分页；删除“左右滑动选择应用”提示和数字页码，底部只用随实际页数生成的短横线，当前页高亮。第一屏 HDMI 控制、涂鸦助手、涂鸦智家、无线网络；保留上方主卡片，不恢复重复底栏。效果图 exec-c7c861ec-7f68-4689-acf7-fef0fbd81c65.png 已获“对了，这个感觉就对了”确认，可以实施；真实编译/设备验收另记。
