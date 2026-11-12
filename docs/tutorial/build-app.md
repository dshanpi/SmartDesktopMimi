# 构建独立桌面应用

本章面向修改 LVGL 桌面和设备 Web 的开发者。它不会重建 Tina 内核，也不会生成一个可以绕过平台兼容检查的万能包。

## 前提

- 干净的固定源码，含实体化 LVGL 和公开依赖。
- 已审核的应用 SDK，包含工具链、sysroot、平台接口与 `sdk-lock.json`。
- 由受控交付记录给出的 SDK 预期 SHA-256。
- Python、CMake、Node/npm 等依赖齐备，输出目录不存在。

## 1. 核对源状态

```sh
git rev-parse HEAD
git status --short
python3 tools/update/build_application.py --help
python3 tools/update/check_contracts.py
python3 tools/update/source_ownership.py
```

修改过桌面后，先提交可追溯的源码，再构建正式候选。构建器拒绝脏源码；不能用手工改构建记录的方法隐藏变更。

## 2. 调用独立应用构建器

下面的 SDK ID、目录和哈希必须替换为实际受控输入。`--sdk-root` 是 SDK **父目录**，工具实际打开 `<sdk-root>/<sdk-id>`。

```sh
MIMI_SDK_ROOT=/absolute/path/to/app-sdks
MIMI_SDK_ID=application-sdk-a133
MIMI_SDK_SHA256=REPLACE_WITH_REVIEWED_SDK_SHA256
MIMI_BUILD_OUTPUT=/absolute/path/to/mimi-builds/new-candidate

python3 tools/update/build_application.py \
  --sdk-root "$MIMI_SDK_ROOT" \
  --sdk-id "$MIMI_SDK_ID" \
  --expected-sdk-sha256 "$MIMI_SDK_SHA256" \
  --output "$MIMI_BUILD_OUTPUT"
```

不要把整个 Tina SDK 路径当作已经导出的应用 SDK；也不要把刚计算出的任意摘要当作审核来源。示例占位值会被拒绝，这是预期行为。

## 3. 构建器依次做什么

1. 核对源码干净、输出是源码外的新目录。
2. 校验 SDK 与外部固定摘要。
3. 执行公开源码审计、接口锁和源码归属检查。
4. 编译 LVGL 桌面。
5. 在设备 Web 工程运行 `npm ci`、`npm run build:device`。
6. 暂存应用树、生成 SBOM 和依赖记录。
7. 再次检查源码/SDK 没变化，写入 `BUILD-COMPLETE`。

## 4. 产物怎么读

| 输出 | 含义 |
| --- | --- |
| `application-tree/` | 待签名的桌面/Web/资源树 |
| `desktop/application-build.json` | 桌面实际构建回执 |
| `candidate-build.json` | 本批源码、SDK、依赖和文件哈希 |
| `sdk-lock.json`、`dependency-lock.json` | 固定依赖来源 |
| `platform-contract-lock.json`、`source-ownership.json` | 契约与源码边界 |
| `BUILD-COMPLETE` | 未签名的应用构建完成 |
| `*.log` | 每个步骤的构建或检查输出 |

本步骤返回的结果明确是 `signed=false`、`uploaded=false`，物理验收为 `not_run`。不要把它直接提供给设备自动更新。

## 5. 失败分支

- SDK missing / outside root：核对父目录、SDK ID 和符号链接边界。
- SDK 摘要不符：检查受控输入是否变化，不能改预期值强行匹配。
- source ownership 失败：确认没有把平台服务放入应用包，或改动了冻结客户端。
- Web 构建失败：使用该源码的 lockfile 与 Node 版本，不拿别处 static 目录替代。
- 输出目录已存在：保留失败日志，为修复后的候选选择新目录。

## 6. 构建之后

先按[验证矩阵](validation.md)验证本次真实产物，再由受控签名/发布流程形成清单和签名包。系统需要新的 ABI 或平台服务时，先满足系统前置要求；不能在应用包内偷偷替换底层。

脚本依据：[build_application.py](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/tools/update/build_application.py) · [发布执行手册](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/RELEASE-RUNBOOK.md)。本轮教程验证没有执行产品交叉构建。
