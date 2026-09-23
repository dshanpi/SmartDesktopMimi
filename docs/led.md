# WS2812 状态灯（产品层）

本文描述 AITVBox 上 10 颗 WS2812 灯带的硬件路径、内核驱动、以及产品层
`service_led` 的所有权、场景仲裁与 UI 联动。

## 1. 硬件与内核

### 硬件

- 灯珠：WS2812 类单线 RGB，本机 **10 颗**。
- 原理图：数据线 **SPI / LEDC 二选一**；当前选型为 **SPI2 MOSI**。
- 用户态设备节点：`/dev/ws2812-leds`（写 GRB 连续帧，每灯 3 字节）。

### 物理方向约定（重要）

板上灯带 **软件下标 0 在视觉右侧**（或等效：沿条方向与常见“左→右递增”相反）。

产品代码统一按 **人眼左右** 映射：

| 视觉效果 | 软件做法 |
|----------|----------|
| 进度从左扫到右（开机 / OTA） | 先点亮高下标：`pixel(n-1-i)` |
| 彗星向右 / 向左 | `set_comet(..., dir)` 与 UI 手指方向对齐，不与“下标增大=向右”死绑 |

改灯效时不要只按 `i = 0..n-1` 当“从左到右”，否则会出现进度/滑动方向反了。

### 内核驱动（Tina SDK）

| 项 | 路径 / 配置 |
|----|-------------|
| 源码 | `kernel/linux-4.9/drivers/leds/leds-ws2812-spi.c` |
| Kconfig | `CONFIG_LEDS_WS2812_SPI`（b6 `openwrt_linux_defconfig` 为 `=y`，编进内核） |
| 匹配 | `compatible = "dshanpi-a1,ws2812"` 或 `"led,ws2812"` |
| 板级 DT | `device/config/chips/a133/configs/b6/linux-4.9/board.dts` → `spi2` 下 `ws2812@0` |
| 推荐 DT 属性 | `leds,number = <10>`；`spi-max-frequency = <6250000>` |

SPI 位带编码：`0 → 0xC0`，`1 → 0xF8`；单次 transfer 含 lead/trail 拉低锁存，
减轻首灯毛刺。

**LEDC（`leds-sunxi` / `sunxi_led*`）** 是另一条硬件通路。当前硬件走 SPI 时，
产品层 **只使用** `/dev/ws2812-leds`，不要用 LEDC sysfs 控同一条灯。

### 板外 demo（非产品路径）

SDK `test/ws2812b_demo/` 仅作驱动验证；与 `lv_backend` **不要同时**占用设备节点。

## 2. 产品架构：灯是公共资源

灯带属于 **系统公共资源**，只允许 **一个写者**（`lv_backend`）：

```text
  service_wifi / service_ai / service_ota
  desktop_dock (UI) ──TOPIC_LED_COMMAND──┐
           │  request / release / on_*   │
           ▼                             ▼
     service_led   （唯一所有者 + 优先级仲裁 + 50Hz 动画）
           │
           ▼
     led_strip_hal （唯一 open / flock / write GRB）
           │
           ▼
     /dev/ws2812-leds  →  内核 leds-ws2812-spi
```

| 规则 | 说明 |
|------|------|
| UI（`lvglsim`）不 open 设备 | 只发 `TOPIC_LED_COMMAND` |
| 业务 service 不写 RGB 裸数据 | 只调 `service_led_*` |
| HAL `flock(LOCK_EX\|LOCK_NB)` | 第二进程占用时 open 失败 |
| 渲染单线程 50Hz | request/release 可多线程，内部 mutex |

`backend_main.c`：**先于** WiFi/AI/OTA 调用 `service_led_init()`，退出时
`service_led_deinit()`（关灯）。

## 3. 源码位置（产品仓库）

| 文件 | 职责 |
|------|------|
| `apps/lv_port_linux/src/system/led_strip_hal.c/.h` | 设备封装（**仅链入 `lv_backend`**） |
| `apps/lv_port_linux/src/system/service_led.c/.h` | 仲裁、场景、业务钩子 |
| `apps/lv_port_linux/src/system/backend_types.h` | `led_cmd_t` |
| `apps/lv_port_linux/src/middleware/middleware.h` | `TOPIC_LED_COMMAND` |
| `apps/lv_port_linux/src/backend_main.c` | init + `handle_led_command` |
| `apps/lv_port_linux/src/ui/desktop_dock.c` | Dock 选中变化 → 滑动灯效 |
| `apps/lv_port_linux/CMakeLists.txt` | `lv_backend` 含 led 源码 + `libm`；UI 排除 `led_strip_hal` |

### 业务挂钩

| 来源 | 挂钩方式 |
|------|----------|
| `service_wifi.c` | 连接中 / 成功 / 断开 / 错误 → `service_led_on_wifi_*` |
| `service_ai_runtime.c` | `publish_status` → `service_led_on_ai_status` |
| `service_ota.c` | `publish_status` → `service_led_on_ota_status`（与 cloud listener 并存） |
| `desktop_dock.c` | 选中下标变化 → `TOPIC_LED_COMMAND` → 冷白彗星（约 380ms） |

## 4. 交互模型：事件优先，默认熄灭

产品目标：**平时灭，有事才亮、亮得能看懂。**

- 无活跃 request 时场景为 **`OFF`（全灭）**，不做常亮氛围呼吸。  
- 启动、联网结果、错误等用 **短时、高对比、尽量纯色** 反馈。  
- **AI 对话进行中**（listening / thinking / speaking）与 **OTA 进行中** 才持续亮。  
- 语音唤醒 **没有独立 WAKE 场景**：只有 AI 状态变为 `listening` 等之后才会上灯（青呼吸等）。

## 5. API 与客户端

### 客户端（`led_client_t`）

`SYSTEM` · `NETWORK` · `BT` · `AI` · `OTA` · `NOTIFY` · `SETTINGS`（UI 滑动等）

### 场景一览

| 场景 | 典型触发 | 视觉 |
|------|----------|------|
| `BOOT` | 启动/重启约 3.5s | 冷白进度 **视觉左→右** 填满 → 全亮 → 渐灭 |
| `OFF` / 默认空闲 | 无 request | **全灭** |
| `WIFI_CONNECTING` | 发起连接 | 纯蓝彗星 |
| `WIFI_OK` / `WIFI_FAIL` | 短 TTL | 绿双闪 / 红橙快闪后灭 |
| `AI_CONNECTING` | 后台连接/准备（含开机） | **不上灯**（避免绿闪后紫尾巴） |
| `AI_LISTENING` | 聆听（含唤醒后进对话） | 青呼吸 |
| `AI_THINKING` / `AI_SPEAKING` | 思考 / 播报 | 紫 / 绿呼吸 |
| `AI_ERROR` | 约 2s TTL | 红脉冲后灭（不粘住） |
| `OTA_PROGRESS` | 下载 0–100 | 纯蓝进度条（同 BOOT 物理方向） |
| `OTA_APPLYING` | 安装 | 纯蓝快闪 |
| `SWIPE_LEFT` / `SWIPE_RIGHT` | Dock 滑动 | 冷白彗星，**跟手指方向**，约 380ms |

### 主要接口

```c
void service_led_init(void);
void service_led_deinit(void);

void service_led_request(led_client_t who, led_scene_t scene, int param, int ttl_ms);
void service_led_release(led_client_t who);

void service_led_set_enabled(bool on);
void service_led_set_brightness(int percent);  /* 0–100，默认约 28 */

void service_led_on_ai_status(const ai_status_t *st);
void service_led_on_ota_status(const ota_status_t *st);
void service_led_on_wifi_connecting(void);
/* ... wifi connected / disconnected / error ... */

void service_led_on_ui_swipe(int dir);          /* dir<0 左，>0 右 */
void service_led_handle_command(const led_cmd_t *cmd);
```

- `param`：如 OTA 进度 0–100。  
- `ttl_ms`：`>0` 到期自动 release；`0` 占到 `release` 或被替换。

### 优先级（高 → 低）

```text
OTA (100) > ERROR (90) > AI (80) > NETWORK (60) > BT (55)
  > SETTINGS/UI 滑动 (45) > NOTIFY (40) > SYSTEM (10)
```

无活跃客户端时渲染 **OFF**（不是 IDLE 呼吸）。

## 6. Dock 滑动联动（UI）

### 链路

```text
desktop_dock 选中下标变化
  → mw_publish(TOPIC_LED_COMMAND, led_cmd_t)
  → backend handle_led_command
  → service_led_handle_command / on_ui_swipe
  → SWIPE_LEFT/RIGHT 彗星（TTL ~380ms）
```

### 方向

- **跟手指**：向右滑 → 灯向右；向左滑 → 灯向左。  
- Dock 内部：手指右滑会减小选中下标，因此 UI 侧对 index delta 做了 **取反** 再映射到 `LED_CMD_*`。

### 可靠性

- 选中变化集中在 `dock_animate_focus_to`（含 **松手吸附换格**）发通知，避免“轻推一格不亮”。  
- 拖动中换格也会通知。  
- 拖动阈值约 **4px**（过小容易当点击、过大则难换格）。  
- AI/OTA 占用时滑动灯效可被盖住（低优先级）。

### UI 命令结构

```c
typedef struct {
    enum {
        LED_CMD_SWIPE_LEFT = 0,
        LED_CMD_SWIPE_RIGHT,
    } action;
} led_cmd_t;
```

## 7. 动画与参数

| 项 | 当前值 / 策略 |
|----|----------------|
| 刷新 | 50Hz |
| 默认亮度 | ~28%（全局缩放） |
| 开机 BOOT | 填满 1.2s + 全亮 1.2s + 渐灭 ~1.1s |
| UI 滑动 TTL | ~380ms |
| AI 错误 TTL | ~2s |
| 色 | 事件尽量单主色，少混色插值 |

## 8. 部署与验证

### 部署

```sh
./scripts/build_apps.sh /path/to/A133-Tina5.0-v0.9
# 产物：build/bin 与 apps/lv_port_linux/build/bin 下的 lv_backend / lvglsim
```

设备正式路径：

```text
/usr/bin/lv_backend
/usr/bin/lvglsim
```

热替换（需同时更新 UI 才有 Dock 灯效）：

```sh
adb push apps/lv_port_linux/build/bin/lv_backend /tmp/lv_backend.new
adb push apps/lv_port_linux/build/bin/lvglsim /tmp/lvglsim.new
adb shell '
  /etc/init.d/aitvbox stop
  cp /tmp/lv_backend.new /usr/bin/lv_backend
  cp /tmp/lvglsim.new /usr/bin/lvglsim
  chmod 755 /usr/bin/lv_backend /usr/bin/lvglsim
  /etc/init.d/aitvbox start
'
```

### 日志

```sh
grep '\[led' /tmp/lv_backend.log
grep SWIPE /tmp/lv_backend.log
```

典型：

```text
[led-hal] opened /dev/ws2812-leds count=10 brightness=28%
[led] service ready brightness=28% enabled=1
[led] grant client=SYSTEM scene=BOOT param=0
[led] ttl expire client=SYSTEM scene=BOOT
[led] grant client=SYSTEM scene=OFF param=0
[led] grant client=SETTINGS scene=SWIPE_RIGHT param=0
```

### 自检

```sh
ls -l /dev/ws2812-leds
readlink /sys/bus/spi/devices/spi2.0/driver   # .../ws2812-spi
pidof lv_backend
# 不要与 ws2812_app 同时跑
```

| 现象 | 检查 |
|------|------|
| 开机进度方向反 | 是否用了旧 `lv_backend`；是否改坏 `n-1-i` 填充 |
| 滑动方向反 | `desktop_dock.c` 中 delta→`LED_CMD_*` 映射 |
| 轻推一格不亮 | 是否新 UI；是否 AI 占用；日志有无 `SWIPE_*` |
| 一直常亮 | 是否旧策略 IDLE 呼吸固件；新版空闲应为 OFF |

## 9. 相关文档

- 运行时总览：[runtime.md](runtime.md)  
- 后端与 middleware：[apps/lv_port_linux/README.md](../apps/lv_port_linux/README.md)  
- OTA 状态机：[ota.md](ota.md)  
