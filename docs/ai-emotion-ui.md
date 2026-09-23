# AI 情绪 UI

本文档记录 AI 自由对话情绪 UI 的设计与实现。涂鸦助手全屏自由对话模式下,中央显示一个动画表情,底部字幕跟随对话状态变化;产品 UI 拥有屏幕所有权,决定何时进入或离开该模式,涂鸦助手事件驱动状态、情绪与字幕更新。

## 当前状态

- **emotion 全链路已打通并经注入验证**:涂鸦云端 skill 下发情绪 -> chatbot 转发 -> backend 解析 -> UI 切换 Lottie 表情。
- **字幕状态机已实现**:字幕随 listening/thinking/speaking/idle 状态变化,回复文本自动去掉说话人前缀,idle 后延迟淡出清空。
- **Lottie 运行时切换已修复**:通过销毁重建 widget 绕过 ThorVG 重复加载不替换的问题。
- **真实云端下发待确认**:代码与连云就绪,但云端是否在每次对话下发情绪取决于 skill 内容,需情感丰富的对话触发(见[验证方法](#验证方法))。

## 参考项目

### Espressif Emote Gen Player

仓库:`https://github.com/espressif2022/esp_emote_gen_player`

价值:提供完整的情绪素材方向(GIF / Lottie 源、设计文档、浏览器打包器、设备播放器),采用 `index.json` 驱动的素材模型,支持即时切换与当前片段播完再切换,通过停帧或循环区间元数据表达 intro/loop/tail 行为。代码许可证 Apache-2.0,比 GPL 示例更适合产品使用。

本地整理后的纯 Lottie 素材包位于 `third_party/aitvbox_emotion_lottie_pack/`。目录中只保留 23 个已清理可见水印的 Lottie JSON,按情绪分类存放;原始压缩包、GIF 和说明图片不再进入项目。

### ElectronBot

仓库:`https://github.com/peng-zhihui/ElectronBot`

价值:三段式情绪动画(进入、循环、返回)的好参考。但其播放器面向 OpenCV / USB / Unity,不适合直接集成;仓库许可证 GPL-3.0,直接产品使用需谨慎。

### Xiaozhi

仓库:`https://github.com/78/xiaozhi-esp32`

价值:架构参考--显示层暴露独立的状态、情绪、聊天消息 API;有用模式--将多种云端情绪名映射到少量核心动画。

## 素材决策

首版采用 Lottie 素材(非 GIF)。理由:

- 产品 UI 是 A133 Linux 上的 LVGL,`lv_conf.defaults` 已开启 `LV_USE_LOTTIE 1`。
- `LV_USE_GIF` 未开启,用 GIF 需额外配置与构建改动。
- Lottie 足以验证布局、情绪映射、涂鸦事件流(若设备性能可接受)。
- 项目只保留并维护 Lottie JSON,不再维护 GIF 后备素材。

素材以**文件**形式安装到运行时文件系统(`/usr/share/aitvbox/emotions/`),不编进 C 数组,便于 OTA 单独替换情绪包而无需重编应用代码。

## 素材水印处理

原始 Espressif Emote Pack 的 Lottie JSON 里有 LottieLab 导出的可见水印层。项目内 `aitvbox_emotion_lottie_pack` 的 23 个文件已全部清理;后续补充或 OTA 替换素材时,仍必须先检查水印再打包进固件。

### 关键结论

- **只删除可见水印层**:`ind == 12345679` 或 `nm == "Group Layer 8"` 的顶层 layer。
- **不要删除名为 `lottielab.com` 的 layer**。当前素材里这个名字虽然像水印,但它是主体动画的一部分或预合成/形状层。曾经误删该 layer 会导致脸部不完整,例如只剩一只眼。
- 清理对象是 Lottie JSON 的 `layers` 顶层数组,不是任意深层字符串替换。
- 清理后必须重新解析全部 JSON,并确认可见水印层数量为 0。

### 当前运行时素材

资源包内 23 个 Lottie 均已清理可见水印。产品首版从中安装以下 10 个文件到 `/usr/share/aitvbox/emotions/`:

| 运行时文件 | 源文件 |
|---------|---------|
| `static.json` | `Neutral & Thinking/static.json` |
| `investigate.json` | `Neutral & Thinking/investigate.json` |
| `ponder.json` | `Neutral & Thinking/ponder.json` |
| `question.json` | `Neutral & Thinking/question.json` |
| `smile.json` | `Positive Emotions/smile.json` |
| `sad.json` | `Negative Emotions/sad.json` |
| `angry.json` | `Negative Emotions/angry.json` |
| `panic.json` | `Surprise & Reactions/panic.json` |
| `shocked.json` | `Surprise & Reactions/shocked.json` |
| `asleep.json` | `Physical & Special States/asleep.json` |

### 如何识别水印层

在 Lottie JSON 顶层 `layers` 中查找:

```json
{
  "ind": 12345679,
  "ty": 4,
  "nm": "Group Layer 8",
  ...
}
```

这个 layer 通常是 LottieLab 导出的可见文字/Logo 水印。删除它后,动画主体仍然完整。

不要用 `grep lottielab` 作为删除依据。当前 JSON 里仍会出现 `lottielab.com`,但它可能是主体层名称:

```text
ind=1 nm=lottielab.com ty=4 hasText=false shapes=1
```

这种 layer 不是要删除的可见水印层。判断标准以 `ind=12345679` / `Group Layer 8` 为准。

### 全量检查

检查资源包中的 JSON 是否有效,并确认没有残留可见水印层:

```sh
cd /home/ubuntu/AI-DeskTopBox
node - <<'NODE'
const fs = require('fs');
const path = require('path');

function walk(dir, out = []) {
  for (const name of fs.readdirSync(dir)) {
    const p = path.join(dir, name);
    const st = fs.statSync(p);
    if (st.isDirectory()) walk(p, out);
    else if (p.endsWith('.json')) out.push(p);
  }
  return out;
}

let failed = false;
for (const file of walk('third_party/aitvbox_emotion_lottie_pack')) {
  const doc = JSON.parse(fs.readFileSync(file, 'utf8'));
  const layers = doc.layers || [];
  const watermark = layers.filter(l => l.ind === 12345679 || l.nm === 'Group Layer 8');
  if (watermark.length) {
    console.log(`${file}: watermark_layers=${watermark.length}`);
    failed = true;
  }
}
process.exit(failed ? 1 : 0);
NODE
```

命令无输出且退出码为 0,表示全部通过。

### 新增素材的清理流程

只处理准备新增的目标文件,不要全量重写已经验证过的素材:

```sh
cd /home/ubuntu/AI-DeskTopBox
node - <<'NODE'
const fs = require('fs');

const file = 'third_party/aitvbox_emotion_lottie_pack/Positive Emotions/laugh.json';
const doc = JSON.parse(fs.readFileSync(file, 'utf8'));
const before = (doc.layers || []).length;
doc.layers = (doc.layers || []).filter(layer =>
  layer.ind !== 12345679 && layer.nm !== 'Group Layer 8'
);
fs.writeFileSync(file, JSON.stringify(doc));
console.log(`${file}: layers ${before}->${doc.layers.length}`);
NODE
```

清理后运行上面的全量检查,再把新文件加入 `packaging/aitvbox-suite/Makefile` 的安装清单并映射到 UI 代码。若误删 `lottielab.com` 主体层,必须从上游原始素材恢复该 JSON,再只删除规定的水印层。

### 设备侧确认

设备运行时加载的是 `/usr/share/aitvbox/emotions/*.json`,不是仓库里的源文件。更新素材后需要重新打包 OTA,或开发阶段手动推送:

```sh
adb shell "mkdir -p /usr/share/aitvbox/emotions"
adb push "third_party/aitvbox_emotion_lottie_pack/Positive Emotions/smile.json" /usr/share/aitvbox/emotions/smile.json
adb shell "/etc/init.d/aitvbox restart"
```

如果屏幕仍看到 `made with lottielab` 字样,优先检查设备上的 `/usr/share/aitvbox/emotions/*.json` 是否已经被新文件覆盖,不要只检查仓库文件。

## 屏幕布局

目标屏幕 1024x768。自由对话布局:

- 顶部安全区 40-60 px。
- 表情动画 560-640 px,水平居中、垂直略偏上。
- 字幕区底部,高 80-128 px,最大宽 760-860 px,字号 30-36 px,白灰色低存在感显示。
- 字幕只显示当前一句话,不显示历史。
- 字幕不显示"我:"/"助手:"等说话人前缀;全屏表情模式里用户主要看脸,文字只作轻量辅助。
- 模式下不显示菜单、应用图标或装饰控件,除非用户退出自由对话视图。

## 情绪映射

UI 侧 `ai_free_chat_view.c` 的 `emote_for_emotion()` 用 `strcmp` **精确匹配大写** emotion 名,将涂鸦云端下发的情绪映射到 10 个 Lottie 素材之一。

### 10 个显示表情(素材文件)

| 显示表情 | 素材文件 | 云端 emotion 名 | 状态默认 |
|---------|---------|----------------|---------|
| 静态脸 | `static.json` | `NEUTRAL` | idle / connecting |
| 笑脸 | `smile.json` | `HAPPY` `LAUGHING` `FUNNY` `LOVING` `RELAXED` `DELICIOUS` `KISSY` `CONFIDENT` `SILLY` `WINK` | speaking |
| 悲伤 | `sad.json` | `SAD` `FEARFUL` `DISAPPOINTED` | |
| 生气 | `angry.json` | `ANGRY` `ANNOYED` | |
| 震惊 | `shocked.json` | `SURPRISE` `SHOCKED` `EMBARRASSED` `TOUCH` | |
| 思考 | `ponder.json` | `THINKING` | thinking |
| 疑问 | `question.json` | `CONFUSED` `LEFT` `RIGHT` | |
| 睡觉 | `asleep.json` | `SLEEP` | |
| 倾听/观察 | `investigate.json` | `WAKEUP` | listening |
| 惊慌 | `panic.json` | (无 emotion 映射) | error / network_unavailable |

共 **26 种云端 emotion 名**映射到 9 个表情;`panic.json` 仅作错误状态默认,无 emotion 映射。

### 状态默认表情

`emote_for_status()` 先查 emotion,匹配到就用对应表情;**匹配不到才回退到 state 默认**:

| AI state | 默认表情 |
|---------|---------|
| `AI_STATE_IDLE` / `AI_STATE_CONNECTING` | static |
| `AI_STATE_LISTENING` | investigate |
| `AI_STATE_THINKING` | ponder |
| `AI_STATE_SPEAKING` | smile |
| `AI_STATE_NETWORK_UNAVAILABLE` / `AI_STATE_ERROR` | panic |

### 大小写与容错

- 涂鸦内核 `emo->name` 是大写(`skill_emotion.h` 中 `EMOJI_HAPPY = "HAPPY"`),UI `strcmp` 大写匹配,backend 原样透传,三者一致。
- 若云端下发未列出的 emotion 名,`emote_for_emotion` 返回 NULL,UI 回退到当前 state 默认表情,不会崩溃。

## 架构与数据流

chatbot 进程与产品 backend 之间用 localhost 双 UDP 端口通信:

```
chatbot (your_chat_bot)                      backend (lv_backend, service_ai_runtime.c)
  │                                            │
  │  5679 (chatbot -> backend) 上报             │  bind 5679, recvfrom
  │  {"runtime":"tuya","state":"speaking"}     │  handle_tuya_message()
  │  {"runtime":"tuya","mode":"free"}          │   -> 解析 state/mode/emotion/text
  │  {"runtime":"tuya","emotion":"HAPPY"}      │   -> 存 ai_status_t, publish TOPIC_AI_STATUS
  │  {"runtime":"tuya","text":"..."}           │
  │ <──────────────────────────────────────────│
  │  5678 (backend -> chatbot) 命令             │  udp_send_json() sendto 5678
  │  {"runtime":"aitvbox","cmd":"enter_free_chat","wake":true}
  │  {"runtime":"aitvbox","cmd":"exit_free_chat"}
```

emotion 完整链路:

```
涂鸦云端 skill 带 emotion
  -> skill_emotion.c 解析 skillContent.emotion,抛 AI_USER_EVT_EMOTION / AI_USER_EVT_LLM_EMOTION
  -> app_chat_bot.c 收到事件:PR_NOTICE("main ui emotion: %s") + UDP 上报 {"emotion":"HAPPY"}
  -> backend service_ai_runtime.c handle_tuya_message():解析 emotion,存 g_ai.status.emotion[32]
  -> publish_status():mw_publish(TOPIC_AI_STATUS, &ai_status_t, ..., MW_DIR_BACKEND_TO_UI)
  -> UI (lvglsim) mw_process_ui_messages() -> dispatch_ui_message() -> on_ai_status()
  -> ai_free_chat_view_set_status() -> apply_status_visuals() -> emote_for_status() -> set_emote_src()
  -> lv_lottie_set_src_file() 切换表情
```

## 关键实现细节

### Lottie 运行时切换:销毁重建 widget

**问题**:LVGL9 的 `lv_lottie_set_src_file()` 内部调 ThorVG `tvg_picture_load(paint, path)`。首次加载(view_open 时)生效,但**后续在已加载的 paint 上重复 `tvg_picture_load` 不会替换内容**,导致切换表情时屏幕不变(表情停在首次的 static.json)。

`lottie_update()` 的刷新链路本身是对的(clear draw_buf + drop image cache + `tvg_canvas_update/draw/sync` + `lv_obj_invalidate`),问题在 `tvg_picture_load` 不替换已加载 paint。

**修复**:`set_emote_src()` 切换时**销毁重建 lottie widget**,让新 src 在全新 paint 上首次加载(走和 view_open 同样的有效路径):

```c
static void set_emote_src(const char *src)
{
#if LV_USE_LOTTIE
    if (!lottie_view || !src) return;
    if (current_emote == src) return;
    current_emote = src;
    lv_obj_t *parent = lv_obj_get_parent(lottie_view);
    lv_obj_delete(lottie_view);
    lottie_view = lv_lottie_create(parent);
    lv_obj_set_size(lottie_view, FREE_CHAT_LOTTIE_SIZE, FREE_CHAT_LOTTIE_SIZE);
    lv_lottie_set_buffer(lottie_view, FREE_CHAT_LOTTIE_SIZE, FREE_CHAT_LOTTIE_SIZE, lottie_buf);
    lv_obj_align(lottie_view, LV_ALIGN_CENTER, 0, -28);
    add_bubble(lottie_view);
    lv_lottie_set_src_file(lottie_view, src);
#endif
}
```

`lottie_buf` 是 static 全局缓冲,销毁重建时复用。每次切换会重建 ThorVG `tvg_animation` / canvas,有少量开销,但表情切换不频繁,可接受。

### 表情播放速度

LVGL Lottie 暴露 `lv_lottie_get_anim()`,可拿到内部 `lv_anim_t`。当前 UI 在每次 `lv_lottie_set_src_file()` 后调用 `apply_lottie_speed(src)`,按素材文件设置整体播放倍率:

| 素材 | 播放倍率 |
|---------|---------|
| `static.json` | 125% |
| `investigate.json` | 135% |
| `ponder.json` | 115% |
| `smile.json` | 110% |
| `sad.json` | 105% |
| `angry.json` | 125% |
| `shocked.json` | 130% |
| `question.json` | 130% |
| `asleep.json` | 90% |
| `panic.json` | 120% |

实现方式是读取 Lottie 默认 duration 后按倍率缩短或拉长:

```c
lv_anim_t *anim = lv_lottie_get_anim(lottie_view);
uint32_t duration = lv_anim_get_time(anim);
duration = (duration * 100U) / speed_pct;
lv_anim_set_duration(anim, duration);
```

这只能改变**整段动画**的播放速度,会同时影响眨眼、嘴巴、脸部移动等全部关键帧。若要“只让眨眼更快,其他动作不变”,需要改 Lottie 素材里的眼睛/眼皮 layer 关键帧,而不是只调 LVGL 播放速度。注意当前 LVGL Lottie wrapper 固定按 60FPS 计算默认 duration,直接改 JSON 顶层 `fr` 不一定生效。

当前倍率是为了让自由对话脸部更灵动的首版调参:倾听、疑问、震惊略快;睡觉略慢;笑脸轻微加速。后续可按设备观感直接调整 `emote_speed_pct()`。

### 字幕状态机

字幕跟随对话状态走(`apply_speech_for_state()`):

| AI state | 字幕行为 |
|---------|---------|
| `LISTENING` | 轻提示"听着呢" |
| `THINKING` | 轻提示"..." |
| `SPEAKING` | 不动(保留 `on_ai_text` 设的回复文本),取消 pending 清空 |
| `CONNECTING` | 不动(保留"正在进入自由对话。"提示),取消 pending 清空 |
| `IDLE` | 延迟 2.5 秒后 300ms 淡出清空(给用户看完最后一句回复) |
| 其他 | 立即清空 |

- `IDLE` 用 `lv_timer` 一次性定时器(2500ms,`repeat_count=1`)延迟清空;若期间状态又变(listening/thinking/speaking),`cancel_clear_speech()` 取消定时器。
- 清空不是硬切,而是先做 300ms text opa 淡出,淡出完成后再把 label 文本设空。
- 新字幕进入时做 160ms 淡入;如果当前已有可见字幕,流式文本更新只替换内容并保持透明度,避免每个分片都闪一次。
- `set_speech()` 会去掉 `我:` / `我：` / `助手:` / `助手：` / `AI:` / `AI：` 前缀,自由对话全屏脸部模式不显示说话人标签。
- 字幕 label 使用固定尺寸 + `LV_LABEL_LONG_DOT`,超长句子以省略号收尾,不把底部区域撑高。
- `ai_free_chat_view_set_text()`(`on_ai_text` 调)设回复时也 `cancel_clear_speech()`,避免回复显示后被 pending 清空抹掉。
- `ai_free_chat_view_close()` 退出时清理定时器。

### emotion 大小写一致性

涂鸦内核 `emo->name` 大写(`skill_emotion.h` `EMOJI_HAPPY "HAPPY"`),UI `strcmp` 大写匹配,backend `handle_tuya_message` 原样 `snprintf` 存入 `ai_status_t.emotion[32]` 并透传。三者全程大写,链路一致。

## 自由对话入口

涂鸦助手页(AI app)暴露产品自有的自由对话入口:

1. 用户打开涂鸦助手 app。
2. 用户点击`自由对话`按钮。
3. 产品 LVGL UI 立即打开全屏情绪视图,发布 `AI_CMD_ENTER_FREE_CHAT`。
4. backend `service_ai_enter_free_chat()` 经 UDP 5678 发 `enter_free_chat` 命令给 chatbot。
5. chatbot `ai_mode_switch(AI_CHAT_MODE_FREE)` 切换模式,进入监听窗口。
6. AI state / text / emotion 事件更新全屏视图。
7. 用户双击全屏视图(或离开 AI app)。
8. UI 发布 `AI_CMD_EXIT_FREE_CHAT`,chatbot 切回 `AI_CHAT_MODE_WAKEUP`,全屏视图关闭。

**关键不变量**:这是临时产品 UI 模式,**不得永久改变涂鸦保存的默认对话模式**。双击退出走 `AI_CMD_EXIT_FREE_CHAT` -> `ai_mode_switch(WAKEUP)`,不走涂鸦原生物理按键双击路径(后者调 `ai_mode_switch_next()` 并保存配置,可能持久化模式)。

## 命令与状态契约

UI -> backend 命令(`backend_types.h`):

```c
typedef enum {
    AI_CMD_START_LISTEN = 0,
    AI_CMD_STOP_LISTEN,
    AI_CMD_GET_STATUS,
    AI_CMD_ENTER_FREE_CHAT,
    AI_CMD_EXIT_FREE_CHAT,
} ai_cmd_action_t;
```

backend -> UI 状态(`ai_status_t`,含 `emotion` 字段):

```c
typedef enum {
    AI_CHAT_MODE_UNKNOWN = 0,
    AI_CHAT_MODE_WAKEUP,
    AI_CHAT_MODE_FREE,
} ai_chat_mode_t;

#define AI_EMOTION_NAME_MAX 32

typedef struct {
    ai_state_t state;
    ai_chat_mode_t chat_mode;
    bool free_chat_active;
    bool control_center_running;
    bool sound_app_running;
    bool bt_paused_by_ai;
    int32_t last_error_code;
    char emotion[AI_EMOTION_NAME_MAX];
} ai_status_t;
```

`emotion` 字段内联在 `ai_status_t` 里,随 `TOPIC_AI_STATUS` 一起发布,无需独立 topic。

## 涂鸦控制通道(补丁 0002)

`third_party/TuyaOpen/apps/tuya.ai/your_chat_bot/src/app_chat_bot.c`（原补丁 `0002-main-ui-free-chat-command-bridge.patch`，已固化进 vendored 源、构建不再 apply）加了 UDP 5678 命令监听 + emotion 转发:

- `enter_free_chat`:中断当前对话(`tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0)`)-> `ai_mode_switch(AI_CHAT_MODE_FREE)` -> 若 `wake=true` 模拟单击进入监听 -> 经 5679 上报 mode ack。
- `exit_free_chat`:中断对话 -> `ai_mode_switch(AI_CHAT_MODE_WAKEUP)` -> 上报 mode ack。
- emotion 转发:监听 `AI_USER_EVT_EMOTION` / `AI_USER_EVT_LLM_EMOTION`,取 `emo->name`,经 5679 上报 `{"runtime":"tuya","emotion":"HAPPY"}`。

backend 以涂鸦 ack 为真相源;可乐观设短期 pending 状态让 UI 响应及时,但必须用 ack 或超时校对。

## 验证方法

### 前提:注入工具 udpsend

设备上 BusyBox `nc` 不支持 UDP(`-u` 不可用),且无 python/socat。`/bin/sh` 是 ash(无 bash `/dev/udp`)。5679 绑定 127.0.0.1,只能设备本地注入。

用产品 SDK 的 aarch64 工具链交叉编译一个 tiny UDP sender,adb push 到 `/tmp/udpsend`:

```sh
CC=/home/ubuntu/A133-Tina5.0-v0.9/prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-1130/toolchain/bin/aarch64-openwrt-linux-gnu-gcc
$CC -O2 -o /tmp/udpsend /tmp/udpsend.c   # 动态链接,设备有 glibc
adb push /tmp/udpsend /tmp/udpsend
adb shell "chmod +x /tmp/udpsend"
# 用法:echo -n '<json>' | /tmp/udpsend 127.0.0.1 5679
```

### 注入验证(可控,已通过)

往 5679 注入模拟 chatbot 上报:

```sh
# emotion 链路
adb shell "echo -n '{\"runtime\":\"tuya\",\"emotion\":\"HAPPY\"}' | /tmp/udpsend 127.0.0.1 5679"
# 字幕状态机
adb shell "echo -n '{\"runtime\":\"tuya\",\"state\":\"listening\"}' | /tmp/udpsend 127.0.0.1 5679"
adb shell "echo -n '{\"runtime\":\"tuya\",\"text\":\"你好\",\"state\":\"speaking\"}' | /tmp/udpsend 127.0.0.1 5679"
adb shell "echo -n '{\"runtime\":\"tuya\",\"state\":\"idle\"}' | /tmp/udpsend 127.0.0.1 5679"
```

验证日志:

```sh
# backend 是否收到并发布
adb shell "grep -E 'emotion=|state=' /tmp/lv_backend.log | tail"
# UI 是否收到(on_ai_status / set_emote 需加 APP_LOGI 调试日志)
adb shell "grep -E 'on_ai_status|set_emote' /tmp/lvglsim.log | tail"
```

### 真实云端下发验证

前提已确认:① chatbot 二进制含 emotion 转发代码(`strings /usr/bin/your_chat_bot_QIO_1.0.1.bin | grep 'main ui emotion'` 找到 `main ui emotion: %s`);② chatbot 已连涂鸦云(`Device MQTT Connected!` + `ai ping/pong` 心跳)。

实际对话后(尤其情感丰富的话,如"讲个笑话"、"我好难过"、"你真烦"):

```sh
# chatbot 是否收到云端情绪并转发
adb shell "grep 'main ui emotion' /tmp/tuya_chat_bot.log | tail"
# backend 是否落地(非 none 即真实下发成功)
adb shell "grep 'emotion=' /tmp/lv_backend.log | tail"
```

出现 `main ui emotion: HAPPY` + `emotion=HAPPY` 即全链路打通。注意:云端不一定每次对话都下发情绪,取决于 skill 内容,可能需多轮对话触发。

## 已知限制与待办

- **真实云端下发**:代码与连云就绪,但云端是否在对话中下发情绪取决于 skill,需实际对话确认。
- **销毁重建开销**:每次切换表情重建 ThorVG animation/canvas,有少量开销。表情切换不频繁可接受;若需更平滑,后续可调研 ThorVG paint 重置或单个 widget 切 src 的正确方式。
- **调试日志**:`on_ai_status` / `set_emote` 的 `APP_LOGI` 是验证期间加的,验证完成后可移除或降级。
- **panic 无 emotion 映射**:`panic.json` 仅作错误状态默认,无云端 emotion 名映射到它。若需 emotion 触发惊慌,可补映射(看涂鸦实际下发的 emotion 名)。
- **字幕形态**:当前是底部单句轻字幕,固定区域省略显示。若未来要歌词式逐字高亮、上下文历史、或语音同步口型,需要单独设计字幕渲染协议。
- **GIF 后备**:`LV_USE_GIF` 未开启,Lottie 性能不足时需显式开启再测 GIF。

## 实现阶段记录

### Phase 1:控制链路验证

- 加 `AI_CMD_ENTER_FREE_CHAT` / `AI_CMD_EXIT_FREE_CHAT`。
- backend `service_ai_enter_free_chat()` / `service_ai_exit_free_chat()`。
- 涂鸦 UDP 5678 命令监听(补丁 0002)。
- chatbot 切 FREE/WAKEUP 模式并经 5679 回 ack。
- adb 日志验证模式切换不持久化涂鸦默认模式。

### Phase 2:UI 外壳

- 涂鸦助手页加可见`自由对话`按钮。
- 全屏 LVGL 视图 + 简单状态视觉 + 字幕。
- `LV_EVENT_DOUBLE_CLICKED` 退出。
- app close / back 也退出自由对话。

### Phase 3:动画原型

- 5 个 Lottie 素材打包到 `/usr/share/aitvbox/emotions/`。
- `lv_lottie_set_src_file(...)` 加载。
- 设备测 CPU / 内存 / 帧率。

### Phase 4:情绪桥接

- chatbot 转发 `AI_USER_EVT_LLM_EMOTION` / `AI_USER_EVT_EMOTION`(补丁 0002)。
- backend 解析 emotion,加 `ai_status_t.emotion[32]`。
- UI 映射 emotion -> 表情。
- **销毁重建修复** Lottie 运行时切换不生效问题。

### Phase 5:打磨与容错

- 字幕状态机(listening/thinking/speaking/idle 跟随 + 延迟淡出清空 + 流式更新防闪烁)。
- 命令超时 / 重试。
- 涂鸦离线或网络不可用时禁用按钮。
- 退出时若 backend 暂停了蓝牙则恢复。
- BT 互斥:AI listening/thinking/speaking 时暂停 A2DP,idle 时恢复。

### 产物

- `build/lv_port_linux/lv_backend`
- `build/lv_port_linux/lvglsim`
- `build/tuyaopen/your_chat_bot_QIO_1.0.1.bin`
- `/usr/share/aitvbox/emotions/*.json`(10 个 Lottie 素材)

## 验证清单

- 涂鸦启动在 `AI_CHAT_MODE_WAKEUP`。
- 点`自由对话`切到 `AI_CHAT_MODE_FREE`。
- `wake=true` 时不需说唤醒词即进入 LISTEN。
- 双击屏幕切回 `AI_CHAT_MODE_WAKEUP`。
- 双击不把 FREE 存为默认模式。
- 全屏视图退出后无残留对象。
- emotion 视图激活时 text 持续更新。
- Lottie 素材从 `/usr/share/aitvbox/emotions/` 加载。
- 注入 emotion 后表情切换(smile/sad/angry/shocked 等)。
- 字幕跟随 state 变化,idle 后延迟淡出清空。
- 动画播放期间 CPU 与 UI 响应可接受。

## 打包方向

- 素材以文件形式安装到 `/usr/share/aitvbox/emotions/`,便于 OTA 替换。
- 不把大素材编进 C 数组(启动与内存不可控)。
- 保留 manifest 描述素材映射、循环元数据、别名(后续可加)。
