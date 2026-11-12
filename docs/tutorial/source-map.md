# 源码归属与平台移植

选择正确仓库是修改功能的第一步。公共桌面、私有产品接入与板级 SDK 各有职责，不能把整个开发目录直接镜像到公开仓库。

## 三个来源

| 仓库 | 维护内容 | 不包含/不替代 |
| --- | --- | --- |
| `dshanpi/SmartDesktopMimi` | 共享桌面、普通应用、通用 IoT、更新工具和公开资料 | 完整 Tina SDK、私有工厂数据 |
| `dshanpi/SmartDesktopMimi-Private` | 涂鸦/100ask 一机一码适配、生产工具与组合来源锁 | 第二份公共桌面 |
| `100askTeam/A133-AIDesktop-Tina5` | 标准 `overlay/`、`meta/`、应用脚本与板级差异 | 独立的公共应用 Release 入口 |

后两项需要相应授权。文档提到它们的职责，不代表读者自动具有访问和再分发权限。

## 按改动选择位置

- 改布局、交互、文案：优先公共桌面，检查 UI 与后端协议边界。
- 增加普通用户应用：使用 APP SDK 与权限模型。
- 改 HDMI/音频/无线驱动：板级系统与 Provider，单独系统构建。
- 改私有云身份或涂鸦授权：私有适配，保持公共禁用行为。
- 改升级兼容或签名：同时核对构建、发布、设备安装三处一致性。

`tools/update/source_ownership.py` 和冻结来源清单防止应用包带入底层服务。移动文件夹不能自动证明架构拆分已经完成。

## 公共与私有版本如何组合

公共编译使用 `MIMI_PRODUCT_EDITION=public`，禁用 100ask 私有接入。私有编译显式选择 private，并提供固定 `MIMI_PRIVATE_PRODUCT_ROOT` 与 `AITVBOX_100ASK_SDK_ROOT`。受限 Tuya 库与离线工具通过授权外部输入提供。

两种通用镜像均不包含单机 License、设备 secret、工厂数据库或签名私钥。私有系统装入的是获准分发的程序，逐台身份仍在制造/运行时受控写入。

## 新平台移植顺序

1. 登记实际 SoC、PCB 修订、存储和显示变体。
2. 准备平台 SDK、工具链、sysroot、运行库和加载器。
3. 实现 Provider，冻结接口与能力查询语义。
4. 构建系统基线、更新器信任和真实设备身份。
5. 为该目标构建应用，核对完整兼容条目。
6. 执行该平台的启动、输入、功能、更新、回退、断电和数据验收。

T527、RK3576 不能继承 A133 的通过结果；已有 A527 骨架不等于 T527 已适配。两个平台同为 aarch64 也不能共用未经验证的二进制包。

Ubuntu x86_64 用于原生开发/验收时，有自己的运行依赖和 deb/Native APP 规则；A133 继续使用 Tina 系统集成。开发主机的构建选择不进入用户桌面。

## 公开源码审计

再分发前核对许可与逐文件资源清单，排除 vendor 库、工具缓存、产物、私有归档和设备数据。SBOM 与 SHA-256 描述“包含什么”，不能替代合法来源和许可确认。

查阅：[产品版本](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/PRODUCT-EDITIONS.md) · [源码归属](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/SOURCE-OWNERSHIP.md) · [公开导出规则](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/public-release.json) · [移植说明](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/zh-CN/porting.md)。
