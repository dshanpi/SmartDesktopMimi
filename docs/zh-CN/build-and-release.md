# 编译与发布

## 环境准备

主机安装 Git、Python 3、Make、CMake、rsync、squashfs-tools、e2fsprogs 及厂商 SDK
依赖。SDK 必须放在产品仓库之外，产品构建只借用工具链和 sysroot。

```bash
git submodule update --init --recursive
export AITVBOX_A133_SDK=/home/ubuntu/A133-Tina5.0-v0.9
python3 tools/aitvbox.py doctor --platform a133
```

`doctor` 检查 SDK 标记和仓库输入。骨架平台在 SDK 与实现通过验证前固定返回退出码 2。

## A133 标准流程

```bash
python3 tools/aitvbox.py configure --platform a133 --config release
python3 tools/aitvbox.py test --platform a133 --scope architecture
python3 tools/aitvbox.py build --platform a133 --clean
python3 tools/aitvbox.py test --platform a133 --scope all
python3 tools/aitvbox.py package --platform a133 --check-only
python3 tools/aitvbox.py package --platform a133
```

迁移期间配置写到 `out/a133/release`，应用和发布产物仍沿用现有 `build/`。正式发布默认
拒绝有已跟踪修改的工作树；临时工程包必须显式使用 `--allow-dirty` 并写入 manifest。

烧录前检查 `BUILD-COMPLETE` 和 `SHA256SUMS`，然后执行：

```bash
python3 tools/aitvbox.py flash --platform a133 \
  --image build/releases/<release>/firmware/a133_linux_b6_uart0.img
```

该命令会认证并执行已审核的 boot-chain gate，但不自动操作 PhoenixSuit/LiveSuit。操作员
必须选择指定测试板和镜像完成全量烧录，再执行板端清单。禁止无明确 action 直接调用 Tina
`build.sh`。

发布记录必须包含 Git、脏状态、SDK/toolchain、组件与固件哈希、命令和测试报告。无论成功
失败都要恢复 SDK 配置。公开 tag 只能来自审计后公共快照的全新 clone 构建。
