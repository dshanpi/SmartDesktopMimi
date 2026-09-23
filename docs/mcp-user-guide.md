# AITVBox 电脑控制与 MCP 使用手册

## 1. 实际连接关系

```text
被控 Windows/Linux HDMI OUT
  -> AITVBox HDMI IN -> 板端持续截图

AITVBox USB Device Type-C
  -> 被控电脑 USB -> 标准 HID 键盘/鼠标
```

AITVBox 有两种电脑控制模式：

| 模式 | 谁运行大模型 | 主要入口 |
|---|---|---|
| 板端本地助手 | A133 上的 `aitvbox-agentd` | IPKVM 浏览器顶部“AI HDMI MCP”工作区 |
| 外部 MCP | 用户自己的 MCP Host | ADB/SSH/本机 stdio 启动 `aitvbox-mcpd` |

普通最终用户使用第一种模式。`mcpd` 是板端截图/HID 工具层，不是让最终用户自己
部署模型服务；外部 MCP 主要用于研发、调试或高级集成。两种模式共享同一套 HDMI
采集和 USB HID 安全约束，不能同时争用输入控制权。

## 2. IPKVM 浏览器本地助手

1. 将电脑 HDMI 输出接到板子 HDMI IN。
2. 将板子 USB Device Type-C 接到同一台电脑。
3. 在电脑浏览器打开设备 IPKVM，完成密码登录。
4. 打开顶部“AI HDMI MCP”工作区，保存 HTTPS Base URL（也兼容完整的
   `chat/completions` endpoint）和模型名。
5. 输入自己的多模态模型密钥并保存；支持 `sk-`、`ark-` 等服务商格式。
6. 输入任务，例如“打开终端并显示系统版本”，点击“开始 AI 控制”。
7. 浏览器的 1080p 直播始终保持连接；随时点击“急停”可终止任务并释放 HID。
8. 任务启动时，A133 物理屏会自动打开“HDMI MCP”应用，并同步显示用户任务、
   HDMI 抓图、模型决策、HID 执行、完成和错误事件。

首次保存密钥时，系统自动使用产品预设的 OpenAI-compatible HTTPS endpoint 和
多模态模型。用户界面不要求填写 MCP Host 命令。密钥只从浏览器提交一次，原子写入
`/overlay/aitvbox/secrets/model-api-key`，权限 `0600`，不会进入日志、APP 包或
固件，也不会由状态 API 返回浏览器。

直播与分析使用同一个采集进程，但属于两条互不抢占的支路：

```text
HDMI 1080p60 -> H.264 -> WebRTC -> 浏览器持续直播
       |
       +-- AI 明确请求时复制一帧 -> 独立 JPEG 工作线程
  -> HTTPS 多模态模型
  -> 严格解析单个受限 JSON 动作
  -> USB HID 执行
  -> 下一张截图
```

AI 不按直播帧率持续取图，不重启 `/dev/video0`，也不关闭浏览器 PeerConnection。

任务默认最多 20 步，服务端硬上限 30 步。模型只能返回白名单动作，鼠标位移、
按键码和按钮范围均校验；退出、错误和急停都会释放 HID。

多显示器场景中，浏览器正在显示的 HDMI 帧是唯一目标，不使用 Windows 的“主屏”或
“副屏”名称判断。网易云任务还具有执行器级可见性门禁：当前帧未明确看到网易云时，
搜索、输入、鼠标、滚轮、普通快捷键和完成动作都会被拒绝；Agent 只能有限次数逐窗
检查，并在每次移窗后重新截图。找不到目标时任务安全失败，不会盲操作另一个显示器。
播放任务必须从当前帧确认网易云正在播放，只有搜索结果不算成功。

管理员需要更换兼容服务或模型时：

```sh
adb shell aitvbox-agentctl configure \
  https://provider.example/v1/chat/completions \
  vision-model
```

该服务必须支持 HTTPS、Bearer API key、Chat Completions 风格的多模态
`image_url` 输入，并返回协议要求的动作 JSON。

方舟 Coding Plan 的 OpenAI-compatible Base URL 可直接填写
`https://ark.cn-beijing.volces.com/api/coding/v3`，Agent 会自动补齐
`/chat/completions`。`doubao-embedding-vision` 只提供向量化能力，不能生成键鼠
动作；可以保存并单独验证其 Embeddings API，但控制服务会拒绝用它启动电脑任务。
HDMI 控制需选择同时支持图片输入和文本/JSON 输出的生成模型。

## 3. 板端 MCP 工具

`/usr/bin/aitvbox-mcpd` 是标准输入/输出 JSON-RPC MCP Server，不监听公网 TCP。
实现 `initialize`、`notifications/initialized`、`tools/list` 和 `tools/call`。

| 工具 | 参数 | 作用 |
|---|---|---|
| `computer.capture` | `{}` | 返回当前 HDMI JPEG |
| `computer.key` | `keycode`, 可选 `modifier` | 按下并释放一个 USB HID 键 |
| `computer.mouse` | `dx`,`dy`，可选 `buttons`,`wheel` | 相对鼠标操作 |
| `hardware.capabilities` | `{}` | 查询真实可用硬件 |
| `safety.stop` | `{}` | 停止自主任务并释放 HID |

范围：

- `keycode`: `0..101`
- `modifier`: `0..255`
- `dx`, `dy`, `wheel`: `-127..127`
- `buttons`: `0..7`

当本地自主任务正在运行时，外部 `computer.key` 和 `computer.mouse` 会被拒绝，
避免两个控制者同时操作电脑。`safety.stop` 始终用于抢占停止。

## 4. 仓库客户端

通过 ADB 查看工具：

```sh
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport adb tools
```

多台设备时加 `--serial SERIAL`。常用调用：

```sh
# 保存 HDMI 截图
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport adb capture --output screen.jpg

# HID Usage 0x04，即字母 A
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport adb key 4

# 相对移动
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport adb mouse 20 10

# 能力发现和急停
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport adb capabilities
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport adb stop
```

SSH 调用：

```sh
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport ssh \
  --host root@DEVICE_IP \
  capabilities
```

板端本机调试：

```sh
python3 tools/mcp/aitvbox_mcp_client.py \
  --transport local \
  --program /usr/bin/aitvbox-mcpd \
  tools
```

## 5. 第三方 MCP Host

支持命令型 stdio Server 的 Host 可配置成：

```json
{
  "mcpServers": {
    "aitvbox": {
      "command": "adb",
      "args": ["shell", "/usr/bin/aitvbox-mcpd"]
    }
  }
}
```

指定设备：

```json
{
  "command": "adb",
  "args": ["-s", "DEVICE_SERIAL", "shell", "/usr/bin/aitvbox-mcpd"]
}
```

SSH：

```json
{
  "command": "ssh",
  "args": ["root@DEVICE_IP", "/usr/bin/aitvbox-mcpd"]
}
```

ADB 必须事先授权；SSH 必须免交互认证。量产设备建议 USB 模式为 `authorized` 或
`disabled`，不要使用免认证 `development`。

## 6. 管理和排查

```sh
adb shell aitvbox-agentctl status
adb shell aitvbox-agentctl stop
adb shell aitvbox-agentctl clear
adb shell ls -l /dev/hidg0
adb shell ls -l /var/run/aitvbox/capture.sock
```

- 没有画面：检查 HDMI 信号、`aitvbox-kvm-video` 和 capture socket。
- Windows/Linux 没有输入：检查 USB 线连接的是板子 Device 口，以及 HID 枚举。
- 模型返回失败：确认模型具备图像输入能力且 endpoint 协议兼容。
- ADB MCP 卡住：检查授权弹窗和多设备 serial。
- 点击停止后仍有按键状态：执行 `aitvbox-agentctl stop`，再检查 `/dev/hidg0`。
- AI 工作区打开或开始任务时直播不应暂停。如果直播断开，检查
  `aitvbox-kvm-video` 是否仍存活，以及 WebRTC PeerConnection 是否保持
  `connected`；不要另起 `hdmi_preview --service` 抢占采集设备。
