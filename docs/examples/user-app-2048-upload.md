# 2048 用户 UI：浏览器上传安装示例

这个示例验证完整链路：声明式嵌入 UI、受限 native-rpc 业务逻辑、独立持久化空间、
发布者签名，以及从 IPKVM 浏览器上传安装。应用 UI 只能在设备“用户应用”内容区
内绘制和滚动，不能覆盖系统标题栏、应用列表或桌面。

## 1. 生成开发者密钥

在仓库根目录执行：

```sh
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 \
  -out developer.key
openssl pkey -in developer.key -pubout -out developer.pem
chmod 600 developer.key
```

`developer.key` 是私钥，只保留在开发机；浏览器只上传 `developer.pem` 公钥。

## 2. 编译并生成安装包

```sh
python3 tools/aitapp/aitapp.py compile \
  platform/app-sdk/examples/2048 \
  --sdk ~/A133-Tina5.0-v0.9

python3 tools/aitapp/aitapp.py build \
  platform/app-sdk/examples/2048 \
  --key developer.key \
  -o game2048-1.0.0.aitapp

python3 tools/aitapp/aitapp.py verify \
  game2048-1.0.0.aitapp \
  --public-key developer.pem
```

最后一条命令必须显示验签成功，才进入上传步骤。

## 3. 从浏览器安装

1. 登录设备 IPKVM，例如 `http://192.168.1.44/`。
2. 打开顶部“Applications”工作区。
3. 在“Trusted publishers”中填写 `100ask-dev`，选择 `developer.pem`，点击
   “Add publisher key”。
4. 在“Install an application”中选择 `game2048-1.0.0.aitapp`。
5. 首次安装保持“Normal upgrade”，点击“Upload and install”。
6. 页面“Installed applications”应显示 `com.100ask.game2048` 和 `1.0.0`。

同版本重新安装选择“Replace same version”；只有明确回退版本时才选择
“Allow version downgrade”。

## 4. 在设备上打开和验收

在设备桌面打开“用户应用”，左侧选择“2048”。依次点击“新游戏”和方向按钮：

- 标题栏和返回按钮始终由系统持有；
- 2048 的文本、按钮和结果只在右侧内容卡片内显示；
- 内容过长时只滚动右侧卡片；
- 退出再打开后棋盘状态仍存在；
- 应用不能跳出容器或覆盖系统 UI。

## 5. 可选：命令行上传

公钥先在浏览器中受信任后，也可以执行：

```sh
python3 tools/aitapp/aitapp.py upload \
  game2048-1.0.0.aitapp \
  --public-key developer.pem \
  --url http://192.168.1.44
```

工具会安全提示输入设备登录密码，不需要把密码写进命令历史。
