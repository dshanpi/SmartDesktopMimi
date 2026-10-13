# Tuya 设备物联 DP Schema

AITVBox 的 Tuya 运行时同时承载 AI 对话和设备物联。UUID/AuthKey 只负责设备身份与接入；
手机面板能显示和下发哪些功能，仍由对应 PID 在涂鸦 IoT 平台配置的 DP Schema 决定。

固件兼容原产品的 DP1/DP3，并对扩展 DP 做运行时 Schema 探测：PID 没有配置的 DP 不会上报，
因此旧产品不会被扩展功能影响。要在智能生活/OEM App 看到全部控制项，需要把下表按原 ID、
类型、读写属性和取值范围加入 PID，并配置相应面板。

> 设备 License（UUID/AuthKey）只证明设备身份，不能修改 PID 的产品模型。产品功能需由该
> PID 的所有者登录 Tuya Developer Platform，在 **Product → Development → Function
> Definition → Custom Function** 中配置。固件会在 Tuya Assistant 页面显示实时覆盖率；
> `2/14 DPs` 表示当前云模型只有 DP1 和 DP3，并非固件功能缺失。

## DP 合约

| DP ID | 建议标识 | 类型 | 权限 | 范围/枚举 | 设备行为 |
|---:|---|---|---|---|---|
| 1 | `switch` | bool | RW | — | AI 助手总开关；关闭时终止当前对话并禁用唤醒 |
| 3 | `volume_set` | value | RW | 0–100, step 1, scale 0 | 扬声器/AI 对话音量 |
| 101 | `screen_brightness` | value | RW | 20–100, step 1, scale 0 | LCD 背光；硬件低于 20% 不可靠，故不接受 0–19 |
| 102 | `hdmi_preview` | bool | RW | — | HDMI 输入持续检测与本机预览 |
| 103 | `bluetooth_speaker` | bool | RW | — | 蓝牙音箱服务开关 |
| 104 | `led_switch` | bool | RW | — | WS2812 状态灯总开关 |
| 105 | `led_brightness` | value | RW | 0–100, step 1, scale 0 | WS2812 亮度 |
| 106 | `temperature` | value | RO | -400–1250, step 1, scale 1, °C | AHT20 温度，0.1 °C |
| 107 | `humidity` | value | RO | 0–1000, step 1, scale 1, % | AHT20 相对湿度，0.1% |
| 108 | `bus_voltage` | value | RO | 0–20000, step 1, scale 0, mV | INA219 总线电压 |
| 109 | `bus_current` | value | RO | -10000–10000, step 1, scale 0, mA | INA219 电流 |
| 110 | `bus_power` | value | RO | 0–100000, step 1, scale 0, mW | INA219 功率 |
| 111 | `assistant_state` | enum | RO | 见下表 | AI 当前状态 |
| 112 | `free_chat` | bool | RW | — | 连续自由对话模式开关 |

DP111 的枚举顺序必须固定为：

| 枚举索引 | 枚举值 |
|---:|---|
| 0 | `idle` |
| 1 | `connecting` |
| 2 | `listening` |
| 3 | `thinking` |
| 4 | `speaking` |
| 5 | `network_unavailable` |
| 6 | `error` |

## 运行时语义

- 云端下发先校验 DP 类型和范围；未知 DP、只读 DP 写入、RAW DP 均拒绝，不原样回显。
- DP1、DP3 和 DP112 由 Tuya 对话进程执行；硬件 DP 通过本机 UDP 桥交给 `lv_backend`，
  由对应硬件服务执行后再发送权威状态快照。
- 设置持久化到现有系统设置文件；连续滑块命令合并为一次延迟写盘，避免频繁 `fsync`。
- MQTT 每次重连都会请求完整状态；运行期间每 30 秒同步一次，同时更新传感器数据。
- Tuya 解绑/恢复出厂会把 Tuya 面板管理的偏好恢复为安全默认值：AI 开、音量 50%、
  屏幕 50%、蓝牙开、HDMI 开、灯带开且亮度 22%。Wi-Fi 凭据不会被该事件删除。
- 不提供云端 Wi-Fi 关闭 DP：从远端关掉设备唯一网络后无法再远端恢复，容易造成永久离线。

## OTA 边界

Tuya OTA 通知会被识别，但固件不直接执行其 URL。产品 OTA 继续使用 100ask Cloud →
`service_ota` → RSA 验签的 SWUpdate A/B 链路，避免新增一条未验签的刷写路径。Tuya OTA
消息中的 URL/HMAC 只记录“是否存在”，不写入日志内容。

## 验收

1. 以机器清单
   `integrations/tuyaopen/config/aitvbox-tuya-dp-schema.json` 为准，在 PID 配完 DP 和面板。
2. 从平台导出/复制实际 Schema JSON 后执行：

   ```bash
   python3 scripts/validate_tuya_dp_schema.py /path/to/actual-schema.json
   ```

   必须得到 `PASS: ... all 14 required DPs ...`。校验器接受 DP 数组、`dps`、`schema`、
   `functions` 或 `result` 包装；不会读取或输出 UUID/AuthKey。
3. 重启设备，Tuya Assistant 页面必须显示 `IoT controls ready: 14/14 DPs`。若仍是
   `2/14`，先确认编辑的是固件 `/etc/aitvbox-tuya-pid` 对应的 PID；必要时按平台要求
   重新绑定设备以刷新面板模型。
4. 分别下发 DP1、3、101–105、112，确认设备真实动作后面板回显实际值。
5. 改动后重启设备，确认设置恢复并在 MQTT 重连后自动同步。
6. 确认 DP106–111 周期更新；拔掉传感器时不上传伪造的零值。
7. 下发越界值、错误类型、未知 DP 和 RAW DP，确认设备拒绝且日志不包含原始负载。
