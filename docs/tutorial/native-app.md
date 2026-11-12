# 为 APP 添加 C 逻辑与持久数据

本章在已掌握[声明式 APP](user-app.md)的基础上创建 A133 Native-RPC APP。需要与设备 ABI 匹配的 Tina 工具链/sysroot，设备固件需具备相应 APP 运行服务。

## 选择 APP 类型

| 类型 | 用途 | 构建要求 |
| --- | --- | --- |
| 声明式 UI | 文本与受限界面动作 | 无需编译 C |
| A133 Native-RPC，schema 2 | 参数化短动作、权限调用、存储 | AArch64 工具链与目标运行库 |
| Ubuntu Native，schema 3 | Ubuntu 原生目标 | 对应 x86_64 工具链与运行时规则 |

本章命令面向 A133。不同运行目标的 Native 包分别构建和校验。

## 创建 Native 工程

在 **Ubuntu 开发主机、产品源码根目录**执行：

```sh
python3 tools/aitapp/aitapp.py init "$HOME/mimi-lab/native-demo" \
  --id com.example.native --name "Native Demo" --publisher example-dev --native
```

生成 `manifest.json`、`src/main.c`、`ui/main.json`。保留初始化生成的运行类型与目标字段；不要将 schema 1 manifest 仅改文件名后用作 Native 工程。

## 编写一个能力查询动作

将 `src/main.c` 改为：

```c
#include "aitvbox/app.h"
#include <string.h>

int main(int argc, char **argv)
{
    const char *action = aitvbox_app_action(argc, argv);
    if (!action)
        return 2;
    if (!strcmp(action, "refresh")) {
        char result[2048];
        int rc = aitvbox_capability_call(
            "hardware.capabilities", "{}", result, sizeof(result));
        return aitvbox_app_reply(rc == 0, rc == 0 ? result : "查询失败");
    }
    return aitvbox_app_reply(false, "不支持的操作");
}
```

每次调用处理一个短动作并返回结果。UI 在 `ui/main.json` 中通过 `app.invoke` 调用它：

```json
{
  "schema": 1,
  "title": "Native Demo",
  "layout": {
    "type": "column",
    "children": [
      {"type": "text", "text": "查询设备能力"},
      {
        "type": "action",
        "label": "刷新",
        "command": "app.invoke",
        "arguments": {"name": "refresh"}
      }
    ]
  }
}
```

`app.invoke` 用于 Native APP。纯声明式 APP 当前只支持受限、无参数的 `capture.snapshot` 动作，且必须声明权限。

## 编译并核对目标 ELF

替换 SDK 的绝对路径：

```sh
python3 tools/aitapp/aitapp.py compile "$HOME/mimi-lab/native-demo" \
  --sdk /absolute/path/to/TinaSDK
file "$HOME/mimi-lab/native-demo/bin/app"
readelf -h "$HOME/mimi-lab/native-demo/bin/app"
```

**检查结果：** 生成 `bin/app`；A133 目标为 AArch64、ELF64、小端，ELF 类型为工具允许的 DYN 或 EXEC。架构不符时检查 SDK 配置和编译器，停止打包上传。

工具读取 Tina 当前配置、工具链与 sysroot。仅有交叉编译器文件而缺少匹配运行库，不能视为 SDK 已准备完成。

## 签名、安装并调用动作

使用前章已有发布者密钥：

```sh
python3 tools/aitapp/aitapp.py build "$HOME/mimi-lab/native-demo" \
  --key "$HOME/.local/share/mimi-demo-signing/developer.key" \
  -o "$HOME/mimi-lab/com.example.native-1.0.0.aitapp"
python3 tools/aitapp/aitapp.py verify "$HOME/mimi-lab/com.example.native-1.0.0.aitapp" \
  --public-key "$HOME/.local/share/mimi-demo-signing/developer.pem"
```

通过后按[APP 安装步骤](user-app.md)上传。在设备打开 Native Demo，点击刷新，核对返回结果与实际硬件能力；检查错误处理和返回首页操作。`src/` 仅用于开发，不进入最终 APP 包。

## 声明权限并添加持久数据

截图、输入和存储分别使用对应权限：

| 权限 | 用途 | 操作约束 |
| --- | --- | --- |
| `capture.snapshot` | 获取画面 | 检查能力和截图目标 |
| `input.keyboard` | 键盘动作 | 使用 HID Usage，测试后释放 |
| `input.pointer` | 相对鼠标动作 | 遵守位移、按钮与滚轮范围 |
| `storage.app` | 本 APP 独立存储 | 使用安全短 key，单值最大 4096 字节 |

权限加入 manifest 的 `permissions`；只声明实际需要的能力。持久化使用 `aitvbox_storage_read` / `aitvbox_storage_write`，可参考项目的 2048 完整示例。

存储检查顺序为：写入测试值 → 退出 APP → 重新打开读取 → 在维护窗口重启后再次读取。记录每一步结果，区分进程内状态与持久化状态。

## 添加中英文资源

在 manifest 的现有字段中加入以下片段，并创建对应 UI 文件：

```json
"localizations": {
  "zh-CN": {"name": "示例应用", "uiEntry": "ui/main.zh-CN.json"},
  "en-US": {"name": "Demo App", "uiEntry": "ui/main.en-US.json"}
}
```

这是 manifest 片段，需合并到已有 JSON 对象；每项同时提供 `name` 与 `uiEntry`。切换系统语言后重新打开 APP，检查相应入口，未提供的语言回退到默认名称和 UI。

## 处理运行错误

Native 无法启动时，依次核对 ELF、动态加载器/依赖、APP runtime 和 `aitvbox-appd` 日志。能力不可用时核对权限及平台开放情况；参数错误时修正调用范围。设备缺少必要运行支持时，使用兼容系统基线后重新验证。

## 查阅依据

[SDK 接口与 Native 规则](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/user-app-development-guide.md) · [完整存储示例](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/platform/app-sdk/examples/2048) · [打包与编译工具](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/tools/aitapp/aitapp.py)
