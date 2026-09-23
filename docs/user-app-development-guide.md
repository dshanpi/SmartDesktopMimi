# AITVBox 用户 APP 开发手册

## 1. 准备开发机

在 Ubuntu 安装 Python 3 和 OpenSSL，并准备本仓库与 A133 Tina SDK：

```sh
sudo apt-get install python3 openssl
cd /home/ubuntu/AI-DeskTopBox
test -d ~/A133-Tina5.0-v0.9
```

每个发布者生成一次 RSA 私钥和公钥。私钥不得上传设备或提交 Git：

```sh
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 \
  -out developer.key
openssl pkey -in developer.key -pubout -out developer.pem
chmod 600 developer.key
```

设备管理员登录 IPKVM 浏览器，在顶部“Applications → Trusted publishers”
填写发布者名称并上传 `developer.pem`。这里只上传公钥，不上传私钥。

`publisher` 是显示/管理标识，真正的所有权由签名公钥 SHA-256 指纹绑定。

## 2. 选择 APP 类型

纯声明式 APP：

```sh
python3 tools/aitapp/aitapp.py init myapp \
  --id com.example.myapp \
  --name "My App" \
  --publisher example-dev
```

带 C 业务逻辑的 native-rpc APP：

```sh
python3 tools/aitapp/aitapp.py init myapp \
  --id com.example.myapp \
  --name "My App" \
  --publisher example-dev \
  --native
```

native 工程结构：

```text
myapp/
├── manifest.json
├── src/main.c
└── ui/main.json
```

`compile` 后生成 `bin/app`。`src/` 只用于开发，不进入 `.aitapp` 包。

## 3. 编写 UI

`ui/main.json` 当前支持 `column`、`text` 和 `action`：

```json
{
  "schema": 1,
  "title": "My App",
  "layout": {
    "type": "column",
    "children": [
      {"type": "text", "text": "设备状态"},
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

native APP 的按钮使用 `app.invoke`。纯声明式 APP 当前只可直接使用无参数
`capture.snapshot` action，且必须同时列在 manifest 的 `permissions` 中；需要
键鼠参数或其他业务逻辑时使用 schema 2 SDK。系统始终在 APP 内容区外保留返回栏。

## 4. 编写 native 逻辑

`src/main.c` 每次只处理一个短动作：

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

需要截图或 HID 时，在 `manifest.json` 声明对应权限：

```json
"permissions": [
  "capture.snapshot",
  "input.keyboard",
  "input.pointer"
]
```

调用示例：

```c
char result[4096];
aitvbox_capability_call("capture.snapshot", "{}", result, sizeof(result));
aitvbox_keyboard(0x04, 0);
aitvbox_pointer(20, 0, 0, 0);
```

键盘使用 USB HID Usage ID，鼠标位移范围为 `-127..127`，buttons 为三位掩码
`0..7`，wheel 为 `-127..127`。被控 Windows/Linux 均使用标准 USB HID，不需要
安装专用驱动。

## 5. 编译

```sh
python3 tools/aitapp/aitapp.py compile myapp \
  --sdk ~/A133-Tina5.0-v0.9
```

工具读取 Tina SDK 当前 `.config`，选择实际 GCC 8.3 AArch64 工具链与 staging
sysroot，生成 PIE `bin/app`。也可用 `--cc` 显式指定兼容编译器。

编译后检查：

```sh
file myapp/bin/app
readelf -h myapp/bin/app
```

应为 AArch64、ELF64、little-endian，类型为 `DYN` 或 `EXEC`。

## 6. 签名和本地验签

```sh
python3 tools/aitapp/aitapp.py build myapp \
  --key developer.key \
  -o com.example.myapp-1.0.0.aitapp

python3 tools/aitapp/aitapp.py verify \
  com.example.myapp-1.0.0.aitapp \
  --public-key developer.pem
```

每次发布必须递增 `manifest.json` 中的语义化版本号。相同版本重装需要
`--replace`，降级需要显式 `--allow-downgrade`。

## 7. 从浏览器上传设备

登录 IPKVM 浏览器，进入顶部“Applications”工作区，选择 `.aitapp` 文件并点击
“Upload and install”。页面也可查看已安装版本、同版本替换、显式降级和卸载应用。
上传接口受设备登录和同源策略保护，设备仍会独立完成签名与策略校验。

也可以在开发机使用同一 HTTP 接口：

```sh
export AITVBOX_PASSWORD='设备登录密码'
python3 tools/aitapp/aitapp.py upload \
  com.example.myapp-1.0.0.aitapp \
  --public-key developer.pem \
  --url http://192.168.1.50
```

自签名 HTTPS 设备可显式加 `--insecure`；也可使用权限为 `0600` 的密码文件：

```sh
python3 tools/aitapp/aitapp.py upload app.aitapp \
  --public-key developer.pem \
  --url https://kvm.example.com \
  --password-file device-password.txt \
  --replace
```

上传工具先在开发机验签，再登录设备的浏览器接口上传；不使用 ADB。上传不会自动
信任公钥，发布者公钥仍需由管理员在 Applications 页面显式加入。

安装完成后，用户在桌面打开“用户应用”。页面每次进入都重新扫描
`/overlay/aitvbox/apps/`，无需重启。

## 8. 常见失败

- `publisher key is not trusted`：管理员尚未加入对应公钥。
- `same-version reinstall requires --replace`：版本未增加。
- `unsupported payload`：包中存在脚本、SO、额外 ELF 或可执行资源。
- `native runtime ...`：未先 `compile`，或 ELF 不是 A133 AArch64。
- APP 按钮提示能力不可用：manifest 未声明权限，或该硬件 broker 尚未开放。
- native APP 无法启动：检查产品内核是否启用 seccomp/filter，以及
  `aitvbox-appd` 日志。

持久化游戏或设置可声明 `storage.app`，再通过
`aitvbox_storage_read`/`aitvbox_storage_write` 访问本 APP 的独立数据区。key
只允许安全短名称，单值最大 4096 字节，写入采用临时文件、`fsync` 和原子替换。
完整示例见 `platform/app-sdk/examples/2048`。

可运行完整示例：

```sh
python3 tools/aitapp/aitapp.py compile \
  platform/app-sdk/examples/2048 \
  --sdk ~/A133-Tina5.0-v0.9
```

从生成密钥到浏览器安装、打开 2048 的逐步演示见
[`docs/examples/user-app-2048-upload.md`](examples/user-app-2048-upload.md)。
