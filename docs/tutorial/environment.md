# 准备 Ubuntu 开发环境

先选择你要做的工作，再安装相应依赖。编写文档、做声明式 APP、编译桌面和制作 Tina 系统镜像，所需输入不同。

## 环境选择表

| 工作 | 公开源码是否够用 | 额外输入 |
| --- | --- | --- |
| 构建文档站 | 是 | Node.js 22.12+ 与 npm |
| 声明式 `.aitapp` | 是 | Python 3、OpenSSL、开发者密钥 |
| Native A133 APP | 否 | 对应交叉编译器/sysroot |
| 独立桌面应用 | 否 | 已审核、固定 SHA-256 的应用 SDK |
| 完整 A133 系统 | 否 | 合法 Tina SDK、标准 overlay、板级及受限依赖 |
| 私有设备功能 | 否 | 授权私有组件与逐台运行时凭据 |

## 1. 获取本教程对应源码

在 Ubuntu 主机执行。使用单独的空目录，保留已有工程：

```sh
mkdir -p ~/mimi-work
cd ~/mimi-work
git clone --branch checkpoint/takeover-20261022 \
  https://github.com/dshanpi/SmartDesktopMimi.git
cd SmartDesktopMimi
git checkout --detach 75500c76e389971bfb3bd0a45ac4bed74e1f8cfa
git rev-parse HEAD
git status --short
```

这里固定历史快照便于复现教程。需要继续开发时从此提交新建工作分支；更新到新版本前先读变更说明。公共快照中的 LVGL 已实体化，不要照内部旧手册盲目拉私有 submodule。

## 2. 安装常见主机依赖

```sh
sudo apt-get update
sudo apt-get install -y git build-essential cmake pkg-config python3 openssl \
  rsync gawk squashfs-tools e2fsprogs util-linux cpio fakeroot \
  libcurl4-openssl-dev libjson-c-dev
```

完整 Tina SDK 还可能要求其配套依赖，以 SDK 手册及项目预检结果补齐。应用 Web 构建需要 Node/npm；Go 版本以所编译模块的 `go.mod` 为准，不能只依赖发行版仓库中的旧版本。

```sh
python3 --version
cmake --version
node --version
npm --version
go version
df -h .
```

产品 Web 历史可复现基线为 Node 22.21.1；文档站要求 Node ≥22.12，本次站点使用 Node 22 系列在 Ubuntu 构建，CI 使用 Node 24。两套 `package-lock.json` 分别管理依赖。

## 3. 规划目录

```text
~/mimi-work/
├── SmartDesktopMimi/        # 公开产品源码
├── TinaSDK/                 # 授权系统构建输入
└── app-sdks/
    └── application-sdk-a133/ # 已审核应用 SDK

~/mimi-builds/               # 新建的应用输出，不放源码树内
~/.local/share/.../          # 受控签名材料，不能进入仓库/SDK/产物
```

项目原维护环境把候选与 SDK 恢复输入放在被忽略的 `.delivery/` 管理；如果沿用该环境应保留原约定，不搬动现有交付资料。上面的目录只给新学习环境说明“源码、SDK、输出分离”。独立应用构建器要求输出是源码树外的新目录。

## 4. 核对输入而不是猜测路径

系统构建显式选择 SDK，并先设置本次审核确定的合法系统版本和固定硬件 overlay 来源。下面的 `REPLACE_WITH_REVIEWED_SYSTEM_VERSION` 是必须替换的占位值：

```sh
export MIMI_SYSTEM_VERSION=REPLACE_WITH_REVIEWED_SYSTEM_VERSION
export MIMI_HARDWARE_SOURCE_ROOT=/absolute/path/to/reviewed-hardware-overlay
./scripts/auto_build_package.sh --check-only --sdk-root /absolute/path/to/TinaSDK
```

预期：缺少工具、SDK 配置、受限库或空间时明确列出并停止。系统构建至少预留 15 GiB，建议 30 GiB 以上；完整 SDK 本身还需要额外存储。

没有 SDK 时，可以继续文档或声明式 APP 实验，不能通过创建空目录、改哈希或跳过来源检查使系统构建“通过”。

## 5. 开始前读什么

依次阅读 `AGENTS.md`、`CURRENT_PROGRESS.md`、`docs/application-update-policy.md` 和与你修改范围相关的门禁记录。最新运行状态在[版本章节](version-status.md)已整理；完整资料以固定源码为准。

下一步：[用户 APP](user-app.md)、[独立桌面](build-app.md)、[系统构建](build-system.md)。
