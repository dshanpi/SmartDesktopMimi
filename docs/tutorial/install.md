# 获取镜像并完成安装前检查

本章面向需要首刷或恢复系统的维护者，完成镜像选择、完整性检查、目标确认及安装后验证。实际写入必须使用随该板型和镜像交付的烧录工具及说明。

## 准备安装资料

开始前取得以下材料：

- 实物板型、PCB 修订、存储介质和容量。
- 对应版本的镜像、官方校验清单、签名及兼容说明。
- 配套烧录工具版本、连接方式、目标选择和恢复说明。
- 需要保留的数据备份，以及可用的串口或其他恢复入口。

若缺少镜像适用范围或板型说明，先补齐资料再写入。本文不假定任意 A133 镜像都适用于该设备。

## 选择匹配的交付版本

打开[官方 Releases](https://github.com/dshanpi/SmartDesktopMimi/releases)，按发布说明核对产品版本、板型、系统前置条件和已完成的验收范围。Draft 需要对应账号权限；页面不可见时，向交付方确认访问权限及文件来源。

当前文档参考快照的发布状态见[版本与适用范围](version-status.md)。实际安装采用交付方明确支持的版本及配套说明。

| 文件类型 | 适用任务 | 安装前重点 |
| --- | --- | --- |
| `SmartDesktopMimi-SystemImage-v<版本>.img.gz` | 首刷或完整系统恢复 | 解压后镜像格式、启动链、分区和目标板 |
| `system.swu` | 已有兼容 A/B 基线上的 OTA | 系统更新器、公钥、分区和当前应用兼容性 |
| `application.tar.gz` 及配套清单/签名 | 独立桌面更新 | 应用更新器、平台接口和运行依赖 |
| `*.aitapp` | 安装用户 APP | 发布者信任与 APP 运行目标 |

下载的源码归档用于阅读和构建，不能直接交给烧录工具。

## 在主机校验下载文件

以下在 **Ubuntu 主机**执行。新建独立目录，将同一版本的必要附件及交付方提供的 `SHA256SUMS` 放入目录。示例镜像名 `vX.Y.Z` 需替换为实际文件名。

```sh
mkdir -p "$HOME/mimi-downloads/reviewed-release"
cd "$HOME/mimi-downloads/reviewed-release"
sha256sum -c SHA256SUMS
gzip -t SmartDesktopMimi-SystemImage-vX.Y.Z.img.gz
```

**检查结果：** 所需文件均显示 `OK`，压缩完整性检查退出码为 0。缺文件、摘要不符或 `gzip` 报错时，重新取得该版本文件后复查，暂不解压写入。

确认可用空间后解压，保留压缩包：

```sh
df -h .
gzip -dk SmartDesktopMimi-SystemImage-vX.Y.Z.img.gz
ls -lh SmartDesktopMimi-SystemImage-vX.Y.Z.img*
```

输出应同时包含 `.img.gz` 和 `.img`。若 `.img` 已存在，先核对其来源，避免覆盖旧资料。磁盘应同时容纳下载包、解压镜像和备份。

Windows 主机可使用 PowerShell 核对单个文件：

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\SmartDesktopMimi-SystemImage-vX.Y.Z.img.gz'
```

将结果与交付方清单比较。哈希校验确认文件一致性；可信签名和目标适配仍按配套发布说明核验。

## 备份现有设备并绑定写入目标

1. 记录当前系统版本、应用版本和设备标识，按[数据备份](settings-data.md)导出必要数据。
2. 记录当前分区、存储容量及恢复方法；确认备份已存放到设备之外。
3. 按烧录说明连接唯一目标，在工具中核对设备位置或标识。
4. 核对工具识别的介质和容量与实物一致。
5. 明确“全量写入”“保留数据”等选项的影响，再执行配套写入流程。

**停止条件：** 识别成错误介质、容量异常、重连后目标改变、分区信息不符或工具报告未知写入结果。保存日志并进入恢复流程，不继续更换目标重试。

## 安装后读回并验收

1. 按配套说明退出烧录模式并启动。
2. 完成[首次启动检查](quickstart.md)，核对桌面、触摸和设置。
3. 读回系统与应用版本，与该次镜像交付清单逐项比较。
4. 检查备份中列出的设置、APP 数据、网络与设备身份是否保留。
5. 记录镜像 SHA-256、工具版本、实际目标和各项结果。

烧录工具显示完成后，仍需完成上述读回。恢复到旧版本时，记录实际恢复版本；新候选的验收另行执行。

## 查阅依据

[系统发布与安装前提](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/RELEASE-RUNBOOK.md) · [历史安装异常及恢复记录](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-board-install-20261029.json)
