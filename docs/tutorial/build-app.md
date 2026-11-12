# 构建独立桌面应用

本章用于修改并构建 LVGL 桌面、Web 界面和共享资源，输出未签名的应用候选。需要固定源码、已审核应用 SDK 和构建依赖；最终设备更新还需签名、兼容检查与验收。

## 准备输入清单

| 输入 | 必须确认 |
| --- | --- |
| 产品源码 | 完整提交、干净工作区、公开依赖齐备 |
| 应用 SDK | SDK ID、工具链/sysroot、平台契约和 `sdk-lock.json` |
| 外部 SDK 摘要 | 来自审核或受控交付记录的预期 SHA-256 |
| 主机工具 | Python、CMake、Node/npm 等与源码要求一致 |
| 输出目录 | 源码树外，尚不存在，有足够空间 |

应用 SDK 是专门导出的输入集合。完整 Tina SDK 目录不能直接作为应用 SDK 父目录使用。

## 检查源码和接口

在 **Ubuntu 开发主机、产品源码根目录**执行：

```sh
git rev-parse HEAD
git status --short
python3 tools/update/build_application.py --help
python3 tools/update/check_contracts.py
python3 tools/update/source_ownership.py
```

**检查结果：** 源码状态干净，契约与归属检查成功。修改过的源码先形成可追溯提交再构建正式候选；冻结接口或归属不符时，按错误修正输入。

## 设置 SDK 与输出参数

以下全部替换为本次实际输入。`--sdk-root` 指向 SDK **父目录**，实际打开的路径为 `<sdk-root>/<sdk-id>`。

```sh
MIMI_SDK_ROOT=/absolute/path/to/app-sdks
MIMI_SDK_ID=application-sdk-a133
MIMI_SDK_SHA256=REPLACE_WITH_REVIEWED_SDK_SHA256
MIMI_BUILD_OUTPUT=/absolute/path/to/mimi-builds/new-candidate

test -d "$MIMI_SDK_ROOT/$MIMI_SDK_ID"
test ! -e "$MIMI_BUILD_OUTPUT"
df -h "$MIMI_SDK_ROOT"
```

前两项应成功退出。预期摘要必须从已审核记录取得；不要将任意现有 SDK 的当前摘要直接当作审核依据。

## 执行独立构建

```sh
python3 tools/update/build_application.py \
  --sdk-root "$MIMI_SDK_ROOT" \
  --sdk-id "$MIMI_SDK_ID" \
  --expected-sdk-sha256 "$MIMI_SDK_SHA256" \
  --output "$MIMI_BUILD_OUTPUT"
```

构建器会依次校验源码/SDK、审计公开来源与接口、编译 LVGL、构建设备 Web、暂存应用树并生成构建记录。结束前会再次核对输入未变化。

构建失败时保留当前输出和日志。修复后使用新的输出目录，避免将两次构建的文件混合。

## 检查生成文件

```sh
test -f "$MIMI_BUILD_OUTPUT/BUILD-COMPLETE"
test -d "$MIMI_BUILD_OUTPUT/application-tree"
python3 -m json.tool "$MIMI_BUILD_OUTPUT/candidate-build.json"
```

| 文件 | 核对内容 |
| --- | --- |
| `application-tree/` | 待签名桌面、Web 和资源 |
| `desktop/application-build.json` | 实际桌面构建回执 |
| `candidate-build.json` | 当前源码、SDK、依赖和文件哈希 |
| `sdk-lock.json`、`dependency-lock.json` | 输入来源及锁定关系 |
| `platform-contract-lock.json`、`source-ownership.json` | 接口和代码边界 |
| `BUILD-COMPLETE` | 本次未签名构建已完成 |

正常构建结果仍为 `signed=false`、`uploaded=false`，物理验收为 `not_run`。进入下一阶段前确认记录确实指向本次源码和 SDK。

## 按报错修复输入

| 失败位置 | 检查与处理 |
| --- | --- |
| SDK 缺失或越界 | 父目录、SDK ID、符号链接及实际目录 |
| SDK 摘要不符 | 对照交付记录核查输入变化，重新审核正确输入 |
| 源码不干净 | 保留改动并形成可追溯提交，再用新输出构建 |
| 契约/源码归属错误 | 核对冻结客户端和平台服务边界 |
| Web 构建失败 | 使用本提交的 lockfile、Node 与依赖，查看对应日志 |
| 输出目录已存在 | 保留旧结果，选择新目录 |

## 进入验收和发布准备

按[验证清单](validation.md)检查本次产物，再进入[发布资料准备](release.md)。应用需要新平台接口或基础库时，先建立相应系统前置条件。

本教程的产品构建命令依据固定源码核对；本轮文档主机未执行产品交叉构建，不能将本站构建结果计为桌面构建结果。

## 查阅依据

[构建器及输出规则](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/tools/update/build_application.py) · [产品发布流程](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/RELEASE-RUNBOOK.md)
