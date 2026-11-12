# 准备 Ubuntu 开发环境

本章按任务准备工具、取得固定源码并规划工作目录。普通 Ubuntu 可以构建文档和声明式 APP；产品交叉编译与系统构建需要另外取得匹配的 SDK。

## 选择本次开发任务

| 任务 | 必要工具 | 额外输入 |
| --- | --- | --- |
| 阅读源码、制作声明式 APP | Git、Python 3、OpenSSL | 公开源码；示例发布者密钥 |
| 构建文档站 | Git、Node.js ≥22.12、npm | 文档所在的 `main` 分支 |
| 编译 A133 Native APP | Python、匹配 GCC 与 sysroot | 授权 Tina SDK 或已确认兼容的工具链 |
| 编译独立桌面 | Python、CMake、Node/npm 等 | 应用 SDK、SDK ID、外部审核摘要 |
| 构建 A133 完整系统 | SDK 手册要求的依赖 | 完整 Tina SDK、固定 overlay、板级与授权组件 |

根据本次任务安装依赖。没有系统 SDK 时，可以先完成声明式 APP，不需要为它安装完整固件工具链。

## 安装基础工具

在 **Ubuntu 开发主机**执行：

```sh
sudo apt-get update
sudo apt-get install -y git python3 openssl
git --version
python3 --version
openssl version
```

需要桌面或系统构建时，再按 SDK 要求安装相关依赖：

```sh
sudo apt-get install -y build-essential cmake pkg-config rsync gawk \
  squashfs-tools e2fsprogs util-linux cpio fakeroot \
  libcurl4-openssl-dev libjson-c-dev
```

这组软件是常见主机依赖；最终以所用 SDK 的预检为准。Web 构建另需 Node/npm，Go 模块的版本要求以对应 `go.mod` 为准。

## 获取教程对应的产品源码

使用新的目录，避免覆盖既有工程。以下固定产品开发快照，供 APP 和产品构建章节使用：

```sh
mkdir -p "$HOME/mimi-work"
cd "$HOME/mimi-work"
git clone --branch checkpoint/takeover-20261022 \
  https://github.com/dshanpi/SmartDesktopMimi.git
cd SmartDesktopMimi
git checkout --detach 75500c76e389971bfb3bd0a45ac4bed74e1f8cfa
git rev-parse HEAD
git status --short
```

**检查结果：** 提交为上述完整 SHA；`git status --short` 无输出。需要修改时，从该提交创建自己的开发分支并按工程要求记录改动。

公共快照已包含实体化 LVGL，按当前源码清单核对依赖。文档站维护使用 `main`，详见[文档发布](site.md)，不要将产品快照与文档分支混用。

## 分离源码、SDK、输出与密钥

```text
~/mimi-work/SmartDesktopMimi/     产品源码
~/mimi-work/TinaSDK/              完整系统 SDK
~/mimi-work/app-sdks/<sdk-id>/    已审核应用 SDK
~/mimi-lab/                      教程示例工程
~/mimi-builds/<new-candidate>/   独立应用构建的新输出目录
~/.local/share/mimi-demo-signing/ 示例发布者密钥
```

独立应用构建器要求输出位于源码树外且尚不存在。系统构建则使用其标准 `build/releases/` 归档。沿用项目维护环境时，候选与恢复输入继续按 `.delivery/` 约定管理；签名私钥始终位于源码、SDK 和产物树之外。

## 核对工具和空间

根据任务执行相应版本检查：

```sh
cmake --version
node --version
npm --version
df -h .
```

仅在需要 Go 模块时另查 `go version`。产品 Web 的历史基线使用 Node 22.21.1；本文档站要求 Node ≥22.12，CI 使用 Node 24，各自使用对应 lockfile。

系统构建至少预留 15 GiB，建议 30 GiB 以上，完整 SDK 本身另占空间。工具缺失或版本不匹配时先补齐，不使用其他版本的旧产物替代构建步骤。

## 进入相应开发流程

修改产品前阅读根目录 `AGENTS.md`、`CURRENT_PROGRESS.md`、`docs/application-update-policy.md` 和相关模块记录。然后选择：

- [声明式 APP](user-app.md)：仅需公开源码、Python 和 OpenSSL。
- [Native APP](native-app.md)：先核对目标工具链和运行 ABI。
- [独立桌面](build-app.md)：先审核应用 SDK 与外部摘要。
- [系统镜像](build-system.md)：先准备 SDK、硬件来源和系统版本。

环境就绪的判据是所选任务的工具与输入齐备；工具安装成功不等于产品构建或设备验收完成。

## 查阅依据

[工程约束](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/AGENTS.md) · [独立应用构建器](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/tools/update/build_application.py)
