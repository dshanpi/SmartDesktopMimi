# AITVBox Platform App SDK v1

AITVBox 用户 APP 不是 Android APK。它是 RSA 签名的 `.aitapp` 包，由系统桌面在
固定内容区内渲染。所有 APP 都必须使用 `presentation=embedded`，系统返回栏始终
保留，APP 无法申请独占全屏。

平台支持两种 APP：

| schema | 适用场景 | 包内容 |
|---|---|---|
| `1` | 文本、按钮和平台能力组合 | 声明式 JSON UI 与资源 |
| `2` | 需要自定义业务逻辑 | 声明式 UI + A133 AArch64 `native-rpc` 程序 |

schema 2 的本机程序不会加载进 LVGL 进程。`aitvbox-appd` 以低权限用户单次启动它，
设置资源上限和 seccomp，程序通过 Unix Socket broker 请求声明过的能力，然后输出
一条严格 JSON 结果。APP 不能直接写文件、联网、打开设备节点、创建子进程或调用
`ioctl`。

## 目录

```text
platform/app-sdk/
├── include/aitvbox/app.h
├── src/app.c
├── manifest.schema.json
├── ui.schema.json
└── examples/
    ├── hello/
    └── native-status/
```

安装后文件位于 `/overlay/aitvbox/apps/<app-id>/`。代码和资源只读，持久数据位于
`data/`；native 程序只能通过 `storage.app` broker 按 key 读写自己的数据，不能
直接打开文件。

## Native RPC ABI

manifest 固定写法：

```json
{
  "schema": 2,
  "runtime": {
    "type": "native-rpc",
    "entry": "bin/app",
    "api": 1
  }
}
```

UI 用 `app.invoke` 触发一个动作：

```json
{
  "type": "action",
  "label": "读取状态",
  "command": "app.invoke",
  "arguments": {"name": "status"}
}
```

程序通过 SDK 读取动作、调用能力并返回结果：

```c
#include "aitvbox/app.h"
#include <string.h>

int main(int argc, char **argv)
{
    const char *action = aitvbox_app_action(argc, argv);
    if (!action)
        return 2;
    if (!strcmp(action, "status")) {
        char result[2048];
        if (!aitvbox_capability_call(
                "hardware.capabilities", "{}", result, sizeof(result)))
            return aitvbox_app_reply(true, "Hardware status is available.");
    }
    return aitvbox_app_reply(false, "Unknown action.");
}
```

公共 API：

```c
const char *aitvbox_app_action(int argc, char **argv);
int aitvbox_app_reply(bool ok, const char *message);
int aitvbox_capability_call(const char *capability,
                            const char *arguments_json,
                            char *result_json, size_t result_size);
int aitvbox_keyboard(unsigned keycode, unsigned modifier);
int aitvbox_pointer(int dx, int dy, unsigned buttons, int wheel);
int aitvbox_storage_read(const char *key, char *value, size_t value_size,
                         bool *found);
int aitvbox_storage_write(const char *key, const char *value);
```

每次 action 最多运行 5 秒，stdout 最多 4 KiB，回复只能是
`{"ok":boolean,"message":"..."}`。action 名最长 64 字节，只允许字母开头以及
字母、数字、点、下划线和连字符。

## 权限

当前已接通 broker：

| manifest 权限 | SDK capability | 状态 |
|---|---|---|
| 无需声明 | `hardware.capabilities` | 可用，只读发现 |
| `capture.snapshot` | `capture.snapshot` | 可用 |
| `input.keyboard` | `input.keyboard` | 可用 |
| `input.pointer` | `input.pointer` | 可用 |
| `storage.app` | `storage.app` | 可用，每个 APP 独立、单值最大 4096 字节 |

`hardware.gpio.*`、`hardware.i2c`、`hardware.spi`、`hardware.uart`、
`hardware.usb` 和 `network.https` 已进入权限白名单，但在完成
R818 板级引脚、电压、总线占用和资源仲裁确认前不会开放执行。声明权限不等于硬件
当前可用，APP 应先查询 `hardware.capabilities`。

## 安全和信任

- 开发者私钥只留在开发机，设备仅保存管理员批准的公钥。
- 包使用 RSA/SHA-256 签名，安装时逐文件校验 SHA-256。
- 安装器拒绝路径穿越、重复路径、符号链接、脚本、SO、额外 ELF 和可执行资源。
- schema 2 只允许 `runtime.entry` 指定的一个 ELF64 little-endian AArch64
  `ET_EXEC/ET_DYN` 文件。
- 安装使用同文件系统 `.new`/`.old` 恢复式切换并在切换前后同步，升级保留
  `data/`，不允许换发布者占用同一 APP ID。
- 运行时再次校验系统所有权、目录不可写、发布者指纹、UID/GID、
  `/proc/<pid>/exe`、父 `appd`、`NoNewPrivs` 和 seccomp 状态。
- native 程序以 UID/GID 65534、无附加组、`no_new_privs`、rlimit 和 seccomp 运行。
- 产品内核必须启用 `CONFIG_SECCOMP=y` 与 `CONFIG_SECCOMP_FILTER=y`；否则 native
  APP 失败关闭，不降级为无沙箱执行。

schema 2 使用共享低权限 UID 与 seccomp，不等同于容器、namespace 或 Landlock
隔离，因此信任模型仍是“管理员批准发布者”，不是接收来源不明的任意二进制。

完整开发步骤见 [user-app-development-guide.md](user-app-development-guide.md)。
