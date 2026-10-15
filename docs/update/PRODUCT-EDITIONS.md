# 公共桌面与私有设备配对

对应 REQ-MIMI-EDITION-062 / PRIVATE-064、065、066。桌面只有一份，编译配置不进入桌面 UI。

| 来源 | 内容 |
| --- | --- |
| dshanpi/SmartDesktopMimi | 共享桌面、普通应用、通用 IoT、语音与更新工具；固定禁用的专有配对接口实现 |
| dshanpi/SmartDesktopMimi-Private | 涂鸦/100ask 一机一码运行适配、生产工具和来源锁；不复制桌面 |
| 100askTeam/A133-AIDesktop-Tina5 | 板级 SDK 标准 overlay、共享底层服务与独立构建输入；不是第二个桌面发布入口 |

公共 Provider 不读涂鸦授权，不编译 100ask 自动配对实现。公共 Tuya 主入口在 SDK、CLI、计时器和网络初始化前返回；公共系统薄包不安装这套设备程序、模型或产品 PID。普通 IoT 云项目接入与这套设备自身授权分开，继续公开。

私有构建显式组合固定来源：100ask 两个 C 文件及涂鸦初始化 C/授权 reader。SDK 中的历史配对副本不作构建回退。缺少组件、来源变化、旧构建目录的版本不符时，构建在清理产物或初始化 SDK 前拒绝。

两种通用镜像均不打入设备凭据；不自动复制 `secrets/` 中的配置。私有设备沿用运行时保护文件和已绑定身份，服务器配置缺省值保留。生产注入工具位于私有仓库，不生成或替换产品可信密钥。

## 开发主机选择

公共构建默认选择 public。显式配置例子：

```sh
export MIMI_PRODUCT_EDITION=public
export AITVBOX_ENABLE_100ASK_CLOUD=OFF
unset MIMI_PRIVATE_PRODUCT_ROOT AITVBOX_100ASK_SDK_ROOT
./scripts/build_apps.sh /absolute/TinaSDK
```

私有构建使用同一个公共源码目录：

```sh
export MIMI_PRODUCT_EDITION=private
export AITVBOX_ENABLE_100ASK_CLOUD=ON
export MIMI_PRIVATE_PRODUCT_ROOT=/absolute/SmartDesktopMimi-Private
export AITVBOX_100ASK_SDK_ROOT=/absolute/100ask-iot-sdk
./scripts/build_apps.sh /absolute/TinaSDK
```

这些目录须独立、绝对且已核对来源；不在环境变量中传设备密钥。工具和 vendor 库仍按既有离线构建手册提供。单独运行暂存后的 Tuya 构建脚本时，还需 `MIMI_PRODUCT_SOURCE_ROOT` 指向当前公共源码，并保留上述版本选择；跳过 prepare 也必须通过来源回执检查。

`build_apps.sh`、`build_firmware.sh` 和 SDK 薄包安装入口共同检查实际 Provider/Tuya 程序哈希、来源回执和版本选择。私有组件不靠目录存在自动启用。系统内固定 `product-edition` 标记用于选择运行路径，未知值拒绝启动；编译好的公共后端仍保持禁用，单改标记不能获得私有能力。

## 已验证与未完成

PRIVATE066 已实际构建公共 30 TU / 私有 32 TU 后端及两种完整 Tuya 程序。新的公共后端 SHA 为 `0075ec88…`，私有后端 `8a8b3a45…` 与原程序字节一致；Tuya 程序分别 `597e0e8a…` / `fd96df76…`。安装夹具使用这些实际程序，验证两版内容及混合产物在复制前拒绝。主机检查不等于设备运行或完整镜像验收。

最终系统产品锁、完整镜像、共享应用更新后私有组件/绑定/数据保留，以及真实授权与设备控制仍需后续验收。用户已将 A133 安装、失联、升级/回退/断电测试后置到新 SDK 环境。旧 rc2/rc23 和 25 个草稿附件保持原来源，不包含本次新隔离，也不覆盖或改名。

产品来源锁与共享更新保留的接续实现/验收范围见[PRODUCT-SOURCE-LOCK](PRODUCT-SOURCE-LOCK.md)。已通过的组件主机验证不代替完整系统镜像或系统OTA产品版本匹配。
