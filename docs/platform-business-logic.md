# AITVBox 平台业务逻辑

## 1. 角色

| 角色 | 可以做什么 |
|---|---|
| 最终用户 | 在 IPKVM 浏览器运行电脑控制任务，在触屏运行已安装用户 APP |
| APP 开发者 | 使用 SDK/工具链开发、签名和上传自己的 APP |
| 设备管理员 | 在浏览器管理发布者公钥、模型 provider、应用安装和卸载 |
| 外部 MCP 用户 | 通过已授权 ADB/SSH 调用板端截图/HID 工具 |

开发者私钥不进设备，模型密钥不进 APP，用户 APP 不获得 root 或设备节点。

## 2. 启动顺序

```text
USB gadget init
  -> 默认仅启用单接口 Report-ID 复合 HID keyboard/mouse
  -> 维修/研发时可显式开启 ADB

hdmi_preview --service
  -> 独占 HDMI capture
  -> /var/run/aitvbox/capture.sock

aitvbox-appd
  -> /var/run/aitvbox/apps.sock

aitvbox-controld
  -> /var/run/aitvbox/control.sock

aitvbox-kvm-video
  -> /var/run/kvm_ctrl.sock
  -> /tmp/kvm_video_stream.sock

aitvbox-ipkvmd
  -> HTTP/HTTPS + WebRTC UDP 40000

lv_backend + lvglsim
  -> 桌面“用户应用”
```

量产默认 `disabled`，Windows/Linux 只看到标准 USB 键盘和鼠标。ADB 的
`authorized` 和 `development` 只保留为显式维修/研发模式。

## 3. 浏览器电脑助手状态机

```text
未配置
  -> 用户在 IPKVM 设置页保存 HTTPS provider 和 sk- 密钥
就绪
  -> 用户提交任务
运行中
  -> HDMI 截图 -> 模型 -> 动作校验 -> USB HID -> 重复
  -> 成功 / 失败 / 用户停止 / 步数达到上限
结束
  -> 强制释放键盘和鼠标按钮
```

`controld` 只接受同 UID 的本地 UI 请求，socket 为 `0660`。密钥和 provider 配置
必须是服务所有、`0600` 的普通文件。Agent 同时只能运行一个实例，外部 MCP 在其
运行时不能注入键盘鼠标；急停仍可用。

模型动作不是任意命令。Agent 只解析受限 JSON，坐标/键码有范围，拒绝尾随内容，
每步重新截图，最多 30 步。网络只允许管理员配置的 HTTPS Chat Completions 兼容
接口。

## 4. APP 发布状态机

```text
aitapp init
  -> schema 1 声明式，或 schema 2 native-rpc
aitapp compile
  -> A133 ELF64 PIE，仅 schema 2
aitapp build
  -> 严格校验 -> SHA256SUMS -> RSA/SHA-256 签名
aitapp verify
  -> 开发机验签
aitapp upload
  -> 登录后的 HTTPS/HTTP multipart 上传 -> 设备 aitvbox-appctl install
浏览器 Applications 页面
  -> 管理可信发布者公钥、上传、查看版本和卸载
安装器
  -> 可信公钥验签 -> 路径/类型/权限/版本检查
  -> 同文件系统原子切换 -> 保留 data
用户应用
  -> 动态扫描 -> 固定系统返回栏 -> 嵌入渲染
```

同一 APP ID 绑定首次安装的发布者公钥指纹，更新不能换签名者；旧版本需要显式允许。
包中除唯一 native entry 外，不允许 ELF、脚本、SO、符号链接或可执行资源。

## 5. APP 运行状态机

声明式 action：

```text
LVGL -> appd platformAction -> 重读已安装 manifest -> 权限检查 -> mcpd
```

native action：

```text
LVGL -> appd invoke
  -> 重读系统所有且不可写的 manifest/指纹/ELF
  -> fork -> UID 65534 + no groups + rlimit + no_new_privs + seccomp
  -> exec bin/app --action NAME
  -> APP SDK -> appd capability
  -> SO_PEERCRED PID + /proc/PID/exe 精确绑定
  -> manifest 权限检查 -> mcpd
  -> APP 返回严格 JSON -> LVGL 显示结果
```

native action 最长 5 秒。seccomp 禁止写文件、AF_INET/AF_INET6 socket、`ioctl`、
mount、ptrace、mknod、clone/fork/vfork。内核不支持 filter 时拒绝运行。

## 6. 浏览器 IPKVM 状态机

```text
启动服务
  -> 密码初始化/登录
浏览器建立 WebRTC
  -> 取得 HDMI owner lock -> 停止桌面预览 -> 启动 A133 H.264
  -> Annex-B H.264 -> WebRTC video
  -> DataChannel HID -> agent.lock 检查 -> hid.lock -> /dev/hidg0
浏览器启动 Agent
  -> 关闭 WebRTC 并释放 HDMI -> controld 启动 agentd
停止并恢复视频
  -> agentd 急停并释放 HID -> 浏览器重新建立 WebRTC
最后会话断开
  -> 停止编码 -> 释放 HDMI owner lock -> 桌面预览可恢复
```

外网模式由 FRPC 转发 HTTPS TCP 和固定 WebRTC UDP `40000`。FRPC 仅管理自己的
子进程，配置持久化为 `frpc.ini`。公网服务必须使用 TLS；原始 HTTP 只能用于可信
局域网。

## 7. 硬件所有权

- `hdmi_preview` 默认持有 HDMI capture；IPKVM 活跃会话通过 owner lock 临时接管。
- `mcpd`/HID 工具是 USB 键鼠操作入口。
- `controld` 决定本地 Agent 是否拥有输入。
- `appd` 决定 APP 是否有权调用能力。
- 浏览器 IPKVM 已接入 HDMI owner lock、Agent lock 和 HID lock。

当前未确认的 GPIO/I2C/SPI/UART 默认关闭。后续每类硬件应有独立 broker，包含资源
ID、参数范围、超时、互斥和审计，而不是把 `/dev` 或 sysfs 暴露给 APP。

## 8. 数据位置

| 数据 | 路径 | 权限/生命周期 |
|---|---|---|
| 模型密钥 | `/overlay/aitvbox/secrets/model-api-key` | `0600`，跨 OTA |
| 模型配置 | `/overlay/aitvbox/secrets/model.conf` | `0600`，跨 OTA |
| APP 信任公钥 | `/overlay/aitvbox/trusted-app-keys/` | 管理员维护 |
| 已安装 APP | `/overlay/aitvbox/apps/<id>/` | 代码只读，原子更新 |
| APP 数据 | `/overlay/aitvbox/apps/<id>/data/` | 更新时保留 |
| 运行 socket/锁 | `/var/run/aitvbox/` | 重启清除 |
| IPKVM 配置 | `/overlay/aitvbox/ipkvm/` | `0700`，跨 OTA |
| FRPC 配置 | `/overlay/aitvbox/ipkvm/frpc.ini` | `0600`，跨 OTA |
