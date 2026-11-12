# 固件获取与安装前检查

本章说明拿到镜像后如何判断它能否用于你的设备。当前记录中存在未完成验收的候选和刷写目标混淆事件，因此不提供自动选择设备或跳过检查的通用刷机命令。

## 1. 选择交付来源

打开[官方 Releases](https://github.com/dshanpi/SmartDesktopMimi/releases)，核对发布类型、版本、目标板和附件说明。受控 Draft 只对有权限的账号可见，匿名访问可能看不到；不要把草稿页面的 404 理解为“换一个镜像即可”。

普通使用者应选择明确适配实物板、附完整验收说明的交付版本。如果发布页只有草稿、缺少目标信息或存在已知失败，应先向设备提供方取得配套镜像和操作说明。

## 2. 识别文件类型

| 文件 | 用途 | 不能用来做什么 |
| --- | --- | --- |
| `SmartDesktopMimi-SystemImage-v<版本>.img.gz` | 完整系统镜像的压缩归档 | 不能按 `.aitapp` 上传 |
| `system.swu` | 已匹配 A/B 基线上的系统 OTA | 不能当作任意旧系统的首刷镜像 |
| `application.tar.gz` + 清单/签名 | 独立桌面更新候选 | 不能替换内核或平台驱动 |
| `*.aitapp` | 用户扩展应用 | 不能升级整个桌面或系统 |
| `source.tar.gz` | 源码归档 | 不是可直接烧录的镜像 |

镜像名不带 A133 是对外命名规则；内部目标仍须明确 A133、PCB 修订、ABI 和产品版本。

## 3. 下载到独立目录并验完整性

在 Ubuntu 主机执行，文件名替换为实际交付版本：

```sh
mkdir -p ~/mimi-downloads/reviewed-release
cd ~/mimi-downloads/reviewed-release
# 将该版本的全部必要附件和官方 SHA256SUMS 放入此目录
sha256sum -c SHA256SUMS
gzip -t SmartDesktopMimi-SystemImage-vX.Y.Z.img.gz
gzip -dk SmartDesktopMimi-SystemImage-vX.Y.Z.img.gz
```

`gzip -dk` 保留原压缩文件，生成新的 `.img`；不要覆盖同名旧产物。磁盘需要同时容纳压缩包、解压镜像与备份。Windows 可用 `Get-FileHash -Algorithm SHA256 <文件>` 对照清单。

SHA-256 只检查文件一致性。来源、可信签名、板型、分区布局、系统与出厂应用配对必须另外核对。不能从刚下载的任意文件自己计算一个哈希，再把它当作“官方预期值”。

## 4. 写入前的检查表

1. 记录实物 PCB、序列号、存储介质和容量，绑定唯一设备。
2. 记录现有系统、应用版本、分区布局及恢复入口。
3. 将用户设置、应用数据和必要设备身份备份到设备之外，核对可读性和哈希；私有内容不要上传到公开 issue。
4. 按交付包核对启动链、分区和兼容清单。
5. 在指定烧录工具中再次核对所选设备，确认目标存储与实物相符。
6. 明确全量写入与保留数据选项的实际语义。按钮名叫 `keep_data` 不等于已经验证保留成功。

如果 A133 eMMC 目标被识别为与实物不符的 SPI NOR、容量突变、重连后设备标识变化，应停止写入并保留日志。这正是历史记录中不能继续盲试的失败分支。

## 5. 完成安装后如何验收

按[首次启动](quickstart.md)检查桌面和触摸，读回系统/应用版本及安装目标；比较升级前后数据；再验证该包声明的功能。不要只因烧录工具显示“成功”就完成验收。

当前 rc33/rc10 在固定快照中没有成功安装结论。恢复旧 rc31/rc8 的记录不能为它们背书。完整依据见[安装事故记录](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-board-install-20261029.json)和[系统发布门禁](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/RELEASE-RUNBOOK.md)。
