# 选择源码仓库并开展平台适配

本章用于确定公共功能、私有接入和板级修改的归属，避免构建时混入不属于本次产物的输入。

## 选择对应源码来源

| 来源 | 维护内容 | 使用条件 |
| --- | --- | --- |
| `dshanpi/SmartDesktopMimi` | 共享桌面、普通应用、通用 IoT、更新和公开资料 | 公共产品源码 |
| `dshanpi/SmartDesktopMimi-Private` | 涂鸦/100ask 私有接入、生产工具与来源锁 | 获得相应访问和分发授权 |
| `100askTeam/A133-AIDesktop-Tina5` | 板级 `overlay/`、`meta/` 和应用脚本 | 获得 SDK 与底层来源授权 |

私有接入复用公共桌面。逐台凭据、签名私钥与工厂数据库不属于任何公共源码或通用镜像。

## 根据修改选择实现位置

1. 修改界面文案、布局或业务交互：在公共桌面中查找页面和客户端调用。
2. 新增普通用户 APP：使用 APP SDK，按权限模型访问平台能力。
3. 修改无线、音频、HDMI 驱动或底层服务：在对应平台与 SDK 来源中实现，按系统发布。
4. 修改私有云身份或授权适配：在私有来源中实现，同时检查公共版禁用行为。
5. 修改升级与签名：同时检查打包、发布与设备激活三个入口。

在 **Ubuntu 主机、产品源码根目录**运行当前归属检查：

```sh
python3 tools/update/source_ownership.py
python3 tools/update/check_contracts.py
```

检查失败时先核对实际改动与锁定清单，再修改相关实现。移动文件目录本身不能证明平台与上层已经解耦。

## 固定公共或私有构建输入

公共系统显式选择 `MIMI_PRODUCT_EDITION=public` 并关闭 100ask 私有接入。私有系统显式选择 private，提供审核后的 `MIMI_PRIVATE_PRODUCT_ROOT` 和 `AITVBOX_100ASK_SDK_ROOT`。

准备输入时逐项记录：仓库提交、组件版本、文件摘要、许可与分发范围。受限 Tuya 库等从授权外部来源取得，不从未知 SDK 副本回退选取。

逐台 License 和身份在制造或运行时注入。构建输入中只包含允许分发的程序和配置结构。

## 按顺序接入新平台

1. 登记 SoC、PCB 修订、存储、显示和触摸变体。
2. 固定工具链、sysroot、动态加载器和运行库。
3. 实现 Provider 与能力查询，确认 Ports/IPC 契约。
4. 建立平台系统基线、更新器信任和真实设备身份。
5. 构建目标应用并生成同一完整条目的兼容清单。
6. 在目标设备验证启动、显示、真实输入及声明提供的硬件功能。
7. 执行应用/系统升级、回退、断电与数据保留路径。

**完成判据：** 每个宣称支持的目标具有自己的输入与验收记录。现有 A527 骨架不代表 T527 已支持，RK3576 也不能继承 A133 验收。Ubuntu x86_64 有独立运行依赖与 deb/Native 规则。

## 公开提交前检查

核对变更列表、源码许可、第三方资源、归属清单和敏感输入边界。SBOM 记录组成，摘要记录文件一致性；再分发权限需结合来源许可判断。

完成归属判断后，分别进入[桌面构建](build-app.md)或[系统构建](build-system.md)。

## 查阅依据

[源码归属](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/SOURCE-OWNERSHIP.md) · [产品组成规则](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/PRODUCT-EDITIONS.md) · [公开源码范围](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/public-release.json) · [平台移植](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/zh-CN/porting.md)
