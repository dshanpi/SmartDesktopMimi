# 制作、签名并安装第一个 APP

本章制作一个显示两行文字的声明式 APP，完成创建、编辑、签名、本地验签和设备安装。主机实验只需 Python 3 与 OpenSSL；设备安装另需兼容固件、IPKVM 管理权限和发布者信任。

## 确认工作目录

完成[开发环境](environment.md)，在 **Ubuntu 开发主机、固定产品源码根目录**执行。示例工程和密钥放在源码树外。

```sh
git rev-parse HEAD
python3 tools/aitapp/aitapp.py --help
mkdir -p "$HOME/mimi-lab"
```

确认帮助中包含 `init`、`build`、`verify` 等子命令。若已有同名示例工程，先选新目录，保留原工程。

## 为新示例发布者生成密钥

仅首次创建示例发布者时执行。已有发布者继续使用原私钥和公钥；不要覆盖现有产品签名材料。

```sh
mkdir -p "$HOME/.local/share/mimi-demo-signing"
chmod 700 "$HOME/.local/share/mimi-demo-signing"
(
  set -e
  umask 077
  MIMI_DEMO_KEYS="$HOME/.local/share/mimi-demo-signing"
  test ! -e "$MIMI_DEMO_KEYS/developer.key"
  test ! -e "$MIMI_DEMO_KEYS/developer.pem"
  openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 \
    -out "$MIMI_DEMO_KEYS/developer.key"
  openssl pkey -in "$MIMI_DEMO_KEYS/developer.key" \
    -pubout -out "$MIMI_DEMO_KEYS/developer.pem"
)
```

**检查结果：** 命令退出码为 0，产生 `developer.key` 私钥和 `developer.pem` 公钥。文件已存在时保护检查会停止；先确认归属后复用，不删除后重建以规避信任检查。

私钥只用于主机签名。交给设备管理员的是公钥，发布者真实所有权以公钥 SHA-256 指纹绑定。

## 创建声明式工程

```sh
python3 tools/aitapp/aitapp.py init "$HOME/mimi-lab/hello" \
  --id com.example.hello --name "你好，小咪" --publisher example-dev
```

检查生成的文件：

```text
hello/
├── manifest.json    应用 ID、名称、版本、发布者和权限
└── ui/main.json     声明式界面
```

`com.example.hello` 是本教程的应用 ID；实际发布时选择自己的唯一 ID。使用编辑器将 `ui/main.json` 替换为：

```json
{
  "schema": 1,
  "title": "你好，小咪",
  "layout": {
    "type": "column",
    "children": [
      {"type": "text", "text": "这是我的第一个小咪应用。"},
      {"type": "text", "text": "使用系统返回栏回到桌面。"}
    ]
  }
}
```

检查 JSON 语法：

```sh
python3 -m json.tool "$HOME/mimi-lab/hello/manifest.json" > /dev/null
python3 -m json.tool "$HOME/mimi-lab/hello/ui/main.json" > /dev/null
```

两条命令退出码均应为 0。出现解析错误时，先修正对应文件和位置，再打包。

## 构建并验签安装包

```sh
python3 tools/aitapp/aitapp.py build "$HOME/mimi-lab/hello" \
  --key "$HOME/.local/share/mimi-demo-signing/developer.key" \
  -o "$HOME/mimi-lab/com.example.hello-1.0.0.aitapp"

python3 tools/aitapp/aitapp.py verify "$HOME/mimi-lab/com.example.hello-1.0.0.aitapp" \
  --public-key "$HOME/.local/share/mimi-demo-signing/developer.pem"

sha256sum "$HOME/mimi-lab/com.example.hello-1.0.0.aitapp"
```

**检查结果：** 构建和验签成功退出，安装包存在。保存包摘要用于记录安装对象；本地验签失败时不上传。

## 在设备上信任公钥并安装

1. 登录目标设备 IPKVM，进入 **Applications → Trusted publishers**。
2. 添加 `example-dev` 发布者及 `developer.pem` 公钥，核对公钥来源。
3. 返回 **Applications**，选择刚生成的 `.aitapp`。
4. 点击 **Upload and install**，等待设备校验和安装结果。
5. 在已安装列表核对应用 ID 和 `1.0.0` 版本。
6. 在触屏打开**用户应用**，刷新或重新进入列表，打开“你好，小咪”。

**检查结果：** 显示两行预期文字，系统返回栏可用，退出后可再次打开。上传不会自动信任包内公钥，管理员信任必须先完成。

## 使用命令行上传（可选）

已完成设备发布者信任时，可在开发主机使用同一 HTTP 接口。准备权限为 `0600` 的设备密码文件，替换 URL 和文件路径：

```sh
python3 tools/aitapp/aitapp.py upload "$HOME/mimi-lab/com.example.hello-1.0.0.aitapp" \
  --public-key "$HOME/.local/share/mimi-demo-signing/developer.pem" \
  --url https://kvm.example.com \
  --password-file /secure/device-password.txt
```

上传完成后仍在设备核对版本和界面。密码文件属于本地受控输入，不提交仓库。

## 修改并发布下一个 APP 版本

1. 修改界面文案。
2. 将 `manifest.json` 的 `version` 改为 `1.0.1`，保持 APP ID 和发布者不变。
3. 使用原私钥构建新的 `com.example.hello-1.0.1.aitapp`，用原公钥验签。
4. 上传后读回 `1.0.1`，打开 APP 检查新文案。

只改包文件名不会改变内部版本。同版本重装和降级属于工具的显式维护选项，普通发布采用递增版本。

## 处理常见安装错误

| 错误 | 处理方法 |
| --- | --- |
| `publisher key is not trusted` | 核对管理员加入的公钥与实际签名公钥 |
| `same-version reinstall requires --replace` | 将 manifest 递增到新版本并重新构建 |
| `unsupported payload` | 检查包内是否混入脚本、SO、额外 ELF 或可执行资源 |
| JSON/schema 校验失败 | 按报错修改 UI 或 manifest，再构建验签 |
| 已安装但列表未更新 | 重新进入或刷新用户应用，核对设备安装结果 |

需要按钮动作、C 逻辑或持久数据时，继续[Native APP 开发](native-app.md)。

## 查阅依据

[用户 APP 格式与安装入口](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/user-app-development-guide.md) · [APP 工具实现](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/tools/aitapp/aitapp.py)
