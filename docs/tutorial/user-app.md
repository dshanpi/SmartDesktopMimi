# 实战：制作并安装用户 APP

本章先做一个无需交叉编译的声明式 APP，再说明 Native-RPC 扩展。目标是跑通“创建 → 校验 → 签名 → 安装 → 打开”，不是更新整个桌面。

## 1. 环境与目录

按[开发环境](environment.md)取得固定源码。以下命令在 Ubuntu 开发主机的**源码根目录**执行。示例工作目录和开发者签名材料放在源码树外：

```sh
sudo apt-get update
sudo apt-get install -y python3 openssl
mkdir -p "$HOME/mimi-lab" "$HOME/.local/share/mimi-demo-signing"
chmod 700 "$HOME/.local/share/mimi-demo-signing"
```

只为新的示例发布者生成一次开发密钥；已有发布者继续使用自己的原密钥，不覆盖产品生产签名体系。

```sh
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 \
  -out "$HOME/.local/share/mimi-demo-signing/developer.key"
openssl pkey -in "$HOME/.local/share/mimi-demo-signing/developer.key" \
  -pubout -out "$HOME/.local/share/mimi-demo-signing/developer.pem"
chmod 600 "$HOME/.local/share/mimi-demo-signing/developer.key"
```

私钥用于本地签名，公钥用于设备验签。只将 `.pem` 公钥交给设备管理员。

## 2. 创建最小 APP

```sh
python3 tools/aitapp/aitapp.py init "$HOME/mimi-lab/hello" \
  --id com.example.hello --name "你好，小咪" --publisher example-dev
```

生成 `manifest.json` 和 `ui/main.json`。使用编辑器将后者替换为：

```json
{
  "schema": 1,
  "title": "你好，小咪",
  "layout": {
    "type": "column",
    "children": [
      {"type": "text", "text": "这是我的第一个小咪应用。"},
      {"type": "text", "text": "安装成功后，可以从系统返回栏回到桌面。"}
    ]
  }
}
```

这个应用不请求硬件权限，适合先验证完整安装链。系统返回栏在应用内容区之外，不要在自定义 UI 中遮挡它。

## 3. 签名与本地验签

```sh
python3 tools/aitapp/aitapp.py build "$HOME/mimi-lab/hello" \
  --key "$HOME/.local/share/mimi-demo-signing/developer.key" \
  -o "$HOME/mimi-lab/com.example.hello-1.0.0.aitapp"

python3 tools/aitapp/aitapp.py verify "$HOME/mimi-lab/com.example.hello-1.0.0.aitapp" \
  --public-key "$HOME/.local/share/mimi-demo-signing/developer.pem"
```

预期：两条命令返回退出码 0，生成 `.aitapp` 并通过验签。若失败先检查 JSON、包内容和密钥对应关系，不上传失败包。

## 4. 管理员信任与安装

1. 登录板端 IPKVM，进入 **Applications → Trusted publishers**。
2. 添加示例发布者和 `developer.pem` 公钥。
3. 返回 Applications，选择 `.aitapp`，点击 **Upload and install**。
4. 检查设备端安装结果和版本，再在小咪桌面打开“用户应用”。
5. 刷新列表、打开“你好，小咪”，确认文字和返回操作正常。

发布者名称是显示标识，真实所有权绑定公钥 SHA-256 指纹。上传包不会自动信任它附带的公钥。

也可使用 CLI；下面使用受保护的密码文件，文件中存放设备登录密码：

```sh
python3 tools/aitapp/aitapp.py upload "$HOME/mimi-lab/com.example.hello-1.0.0.aitapp" \
  --public-key "$HOME/.local/share/mimi-demo-signing/developer.pem" \
  --url https://kvm.example.com \
  --password-file /secure/device-password.txt
```

密码文件权限设为 `0600`，域名替换为实际可信设备地址。CLI 走同一个浏览器 HTTP 接口，仍需要管理员预先信任发布者。

## 5. 修改与升级

修改 UI 后，把 `manifest.json` 的 `version` 从 `1.0.0` 增为 `1.0.1`，用原发布者私钥重新构建、验签和上传。不要仅换文件名而不改 manifest。

用户 APP 工具有显式同版本替换和降级选项，正常发布优先递增版本；这些选项不适用于桌面更新器，不能用来绕过系统信任或硬件匹配。

## 6. 加入 Native-RPC C 逻辑

需要参数化动作、存储或硬件调用时，新建 schema 2 工程：

```sh
python3 tools/aitapp/aitapp.py init "$HOME/mimi-lab/native-demo" \
  --id com.example.native --name "Native Demo" --publisher example-dev --native
python3 tools/aitapp/aitapp.py compile "$HOME/mimi-lab/native-demo" \
  --sdk /absolute/path/to/TinaSDK
file "$HOME/mimi-lab/native-demo/bin/app"
readelf -h "$HOME/mimi-lab/native-demo/bin/app"
```

Native 工程包含 `src/main.c`、`ui/main.json`，编译生成 `bin/app`。A133 目标应核对 AArch64、ELF64、小端和匹配运行库。`src/` 不进入最终包。Ubuntu x86_64 Native 另有显式 schema 3 工具链，不与 A133 混装。

UI 按钮通过 `app.invoke` 调用短动作；权限写在 manifest 的 `permissions`。纯声明式 APP 当前只有受限无参数 `capture.snapshot` 动作，不能照抄 Native 的 `app.invoke` 当成通用声明式动作。

持久化数据声明 `storage.app`，使用 SDK 的读写接口；单值最大 4096 字节，key 受安全名称约束。完整示例见 [2048 APP](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/platform/app-sdk/examples/2048)。

## 常见错误

| 错误 | 处理 |
| --- | --- |
| `publisher key is not trusted` | 核对管理员添加的公钥指纹 |
| `same-version reinstall requires --replace` | 正常发布递增 manifest 版本 |
| `unsupported payload` | 去掉脚本、额外 ELF、SO 或可执行资源 |
| Native runtime 错误 | 先编译并核对目标 ABI、运行依赖 |
| 按钮能力不可用 | 核对 manifest 权限及实际平台能力 |

完整说明：[用户 APP 开发手册](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/user-app-development-guide.md)与[工具源码](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/tools/aitapp/aitapp.py)。
