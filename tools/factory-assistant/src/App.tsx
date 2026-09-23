import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from "react";
import {
  Button,
  Dialog,
  DialogActions,
  DialogBody,
  DialogContent,
  DialogSurface,
  DialogTitle
} from "@fluentui/react-components";
import {
  ArrowClockwise,
  Bluetooth,
  CaretRight,
  Check,
  CircleNotch,
  ClockCountdown,
  Code,
  Cpu,
  Crosshair,
  Database,
  GearSix,
  HandTap,
  HardDrive,
  HardDrives,
  IdentificationCard,
  Info,
  Lightning,
  Memory,
  Microphone,
  Monitor,
  Play,
  Pulse,
  SpeakerHigh,
  TerminalWindow,
  TestTube,
  ThermometerSimple,
  Usb,
  Warning,
  WifiHigh,
  X
} from "@phosphor-icons/react";
import {
  confirmHardwareTest,
  getSnapshot,
  onSnapshot,
  playHardwareTestTone,
  resetRun,
  runMicrophoneHardwareTest,
  scanDevices,
  startHardwareTests
} from "./lib/backend";
import type {
  EventRecord,
  HardwareTestResult,
  HardwareTestStatus,
  StationSnapshot
} from "./types";

const testIcons: Record<string, typeof Usb> = {
  identity: IdentificationCard,
  storage: HardDrive,
  memory: Memory,
  wifi: WifiHigh,
  bluetooth: Bluetooth,
  environment: ThermometerSimple,
  power: Lightning,
  audio: SpeakerHigh,
  "touch-controller": HandTap,
  screen: Monitor,
  touch: HandTap,
  speaker: SpeakerHigh,
  microphone: Microphone
};

const testProtocols: Record<string, { method: string; source: string; rule: string; command: string }> = {
  identity: {
    method: "通过 ADB 读取 Allwinner CPUID 和固件版本文件，并校验格式。",
    source: "/sys/class/sunxi_info/sys_info · /etc/aitvbox-version",
    rule: "CPUID 必须是 32 位十六进制，且固件版本不能为空。",
    command: "sed -n 's/^[[:space:]]*sunxi_serial[[:space:]]*:[[:space:]]*//p' /sys/class/sunxi_info/sys_info | head -1; sed -n 's/^AITVBOX_VERSION=//p' /etc/aitvbox-version | head -1"
  },
  storage: {
    method: "读取 eMMC 总扇区数并乘以 512 字节，换算十进制 GB 与二进制 GiB。",
    source: "/sys/class/block/mmcblk0/size",
    rule: "默认 BOM 为 16 GB，实测字节数不得低于标称容量的 95%。",
    command: "cat /sys/class/block/mmcblk0/size"
  },
  memory: {
    method: "读取 Linux 启动后可见的 MemTotal，换算为 MiB 并与 BOM 比较。",
    source: "/proc/meminfo · MemTotal",
    rule: "默认 BOM 为 2 GB，系统可见内存不得低于标称值的 90%。",
    command: "awk '/^MemTotal:/ {print $2}' /proc/meminfo"
  },
  wifi: {
    method: "校验 wlan0 MAC 地址，并执行一次不连接网络的 2.4G/5G RF 扫描。",
    source: "/sys/class/net/wlan0 · iw dev wlan0 scan",
    rule: "MAC 地址有效且至少扫描到 1 个无线接入点，不要求连接互联网。",
    command: String.raw`mac=$(cat /sys/class/net/wlan0/address 2>/dev/null) || exit 1; state=$(cat /sys/class/net/wlan0/operstate 2>/dev/null); scan=$(iw dev wlan0 scan 2>/dev/null | awk '/^BSS /{n++} /signal:/{if(!seen || $2>best){best=$2;seen=1}} END{if(n>0) printf "%d %.0f",n,best}'); printf '%s\n%s\n%s\n' "$mac" "$state" "$scan"`
  },
  bluetooth: {
    method: "读取 hci0 控制器信息、蓝牙地址和当前运行标志。",
    source: "hciconfig hci0",
    rule: "hci0 必须存在，并处于 UP 或 UP RUNNING 状态。",
    command: "hciconfig hci0 2>/dev/null"
  },
  environment: {
    method: "定位 AHT20 hwmon 实例，分别读取温度和相对湿度原始值。",
    source: "/sys/class/hwmon/*/temp1_input · humidity1_input",
    rule: "温度需在 -20 至 80°C，湿度需在 0 至 100%RH，判定传感器读数有效。",
    command: String.raw`for d in /sys/class/hwmon/hwmon*; do n=$(cat "$d/name" 2>/dev/null); case "$n" in *aht20*) cat "$d/temp1_input" "$d/humidity1_input" 2>/dev/null; exit;; esac; done; exit 1`
  },
  power: {
    method: "定位 INA219 hwmon 实例，同时读取总线电压、电流和实时功率。",
    source: "/sys/class/hwmon/*/in1_input · curr1_input · power1_input",
    rule: "三项数据必须完整，电压大于 0，电流不得为负数。",
    command: String.raw`for d in /sys/class/hwmon/hwmon*; do n=$(cat "$d/name" 2>/dev/null); case "$n" in *ina219*) cat "$d/in1_input" "$d/curr1_input" "$d/power1_input" 2>/dev/null; exit;; esac; done; exit 1`
  },
  audio: {
    method: "读取 ALSA 已注册声卡列表，确认音频编解码设备被内核识别。",
    source: "/proc/asound/cards",
    rule: "声卡列表不能为空；此项只验证设备识别，扬声器音质另行人工确认。",
    command: "cat /proc/asound/cards 2>/dev/null"
  },
  "touch-controller": {
    method: "在内核输入设备表中匹配 gt9xxnew_ts，并解析对应 event 节点。",
    source: "/proc/bus/input/devices",
    rule: "必须同时找到 gt9xxnew_ts 名称和 event 输入处理器。",
    command: "awk '/^N: Name=/{n=$0} /Handlers=.*event[0-9]+/{if(n ~ /gt9xxnew_ts/){print n; print $0; exit}}' /proc/bus/input/devices"
  }
};

const statusLabels: Record<HardwareTestStatus, string> = {
  waiting: "待检测",
  running: "检测中",
  passed: "通过",
  failed: "异常",
  needsConfirmation: "待确认"
};

interface VisualMetric {
  label: string;
  value: string;
  note?: string;
  percent?: number;
}

function clamp(value: number) {
  return Math.max(0, Math.min(100, value));
}

function visualMetrics(test: HardwareTestResult): VisualMetric[] {
  const value = test.value || "";
  if (test.id === "storage") {
    const match = value.match(/([\d.]+) GB.*?([\d.]+) GiB/);
    if (match) {
      const nominal = Number(match[1]);
      const measured = Number(match[2]);
      const nominalGiB = nominal * 1_000_000_000 / 1024 / 1024 / 1024;
      return [{ label: "eMMC 容量", value: `${measured.toFixed(1)} GiB`, note: `${nominal} GB BOM`, percent: clamp(measured / nominalGiB * 100) }];
    }
  }
  if (test.id === "memory") {
    const match = value.match(/([\d.]+) GB.*?([\d.]+) MiB/);
    if (match) {
      const nominalMiB = Number(match[1]) * 1024;
      const measured = Number(match[2]);
      return [{ label: "系统可见内存", value: `${measured.toFixed(0)} MiB`, note: `${match[1]} GB BOM`, percent: clamp(measured / nominalMiB * 100) }];
    }
  }
  if (test.id === "wifi") {
    const match = value.match(/(\d+) 个 AP.*?(-?\d+) dBm.*?([\w-]+)/);
    if (match) {
      const signal = Number(match[2]);
      return [
        { label: "扫描结果", value: `${match[1]} 个 AP`, note: "本次 RF 扫描" },
        { label: "最强信号", value: `${signal} dBm`, note: "-100 至 -30 dBm", percent: clamp((signal + 100) / 70 * 100) },
        { label: "接口状态", value: match[3].toUpperCase(), note: "wlan0" }
      ];
    }
  }
  if (test.id === "environment") {
    const match = value.match(/(-?[\d.]+) °C.*?([\d.]+) %RH/);
    if (match) {
      const temperature = Number(match[1]);
      const humidity = Number(match[2]);
      return [
        { label: "温度", value: `${temperature.toFixed(1)} °C`, note: "AHT20 当前采样", percent: clamp((temperature + 20) / 100 * 100) },
        { label: "相对湿度", value: `${humidity.toFixed(1)} %RH`, note: "AHT20 当前采样", percent: clamp(humidity) }
      ];
    }
  }
  if (test.id === "power") {
    const match = value.match(/([\d.]+) V.*?([\d.]+) mA.*?([\d.]+) W/);
    if (match) {
      return [
        { label: "总线电压", value: `${match[1]} V`, note: "INA219", percent: clamp(Number(match[1]) / 6 * 100) },
        { label: "实时电流", value: `${match[2]} mA`, note: "当前负载" },
        { label: "实时功率", value: `${match[3]} W`, note: "电压 × 电流" }
      ];
    }
  }
  if (test.id === "microphone") {
    const match = value.match(/RMS\s*(-?[\d.]+) dBFS.*?峰值\s*(-?[\d.]+) dBFS/);
    if (match) {
      const rms = Number(match[1]);
      const peak = Number(match[2]);
      return [
        { label: "平均电平", value: `${rms.toFixed(1)} dBFS`, note: "有效阈值 ≥ -50 dBFS", percent: clamp((rms + 90) / 60 * 100) },
        { label: "峰值电平", value: `${peak.toFixed(1)} dBFS`, note: "有效阈值 ≥ -35 dBFS", percent: clamp((peak + 90) / 60 * 100) }
      ];
    }
  }
  if (test.id === "bluetooth" && value) return [{ label: "蓝牙地址", value, note: "hci0 · UP RUNNING" }];
  if (test.id === "audio" && value) return [{ label: "已识别声卡", value, note: "ALSA card 0" }];
  if (test.id === "touch-controller" && value) return [{ label: "输入节点", value, note: "gt9xxnew_ts" }];
  if (test.id === "identity" && value) return [{ label: "固件版本", value: value.replace(/^固件\s*/, ""), note: "CPUID 格式同时通过" }];
  return [];
}

function EventIcon({ level }: { level: EventRecord["level"] }) {
  if (level === "success") return <Check weight="bold" />;
  if (level === "warning") return <Warning weight="fill" />;
  if (level === "error") return <X weight="bold" />;
  return <Info weight="fill" />;
}

function TestStatus({ status }: { status: HardwareTestStatus }) {
  return (
    <span className={`test-status status-${status}`}>
      {status === "running" && <CircleNotch className="status-spinner" weight="bold" />}
      {status === "passed" && <Check weight="bold" />}
      {status === "failed" && <X weight="bold" />}
      {status === "needsConfirmation" && <ClockCountdown weight="bold" />}
      {statusLabels[status]}
    </span>
  );
}

function TestNavigatorRow({ test, index, selected, onSelect }: { test: HardwareTestResult; index: number; selected: boolean; onSelect: () => void }) {
  const Icon = testIcons[test.id] || TestTube;
  return (
    <button className={`navigator-row test-${test.status} ${selected ? "selected" : ""}`} onClick={onSelect} type="button">
      <span className="test-step-number">{String(index + 1).padStart(2, "0")}</span>
      <span className="test-icon"><Icon weight="duotone" /></span>
      <span className="navigator-copy"><strong>{test.label}</strong><small>{test.value || test.summary}</small></span>
      <TestStatus status={test.status} />
      {test.durationMs !== undefined && <span className="test-duration">{test.durationMs} ms</span>}
      <CaretRight className="navigator-caret" weight="bold" />
    </button>
  );
}

function RunMonitor({ tests, manualTests, connected }: { tests: HardwareTestResult[]; manualTests: HardwareTestResult[]; connected: boolean }) {
  const currentIndex = tests.findIndex(test => test.status === "running");
  const current = currentIndex >= 0 ? tests[currentIndex] : undefined;
  const completed = tests.filter(test => test.status === "passed" || test.status === "failed").length;
  const automaticFinished = tests.length > 0 && completed === tests.length;
  const manualActive = manualTests.some(test => test.status === "needsConfirmation");
  const manualFinished = manualTests.length > 0 && manualTests.every(test => test.status === "passed" || test.status === "failed");
  const anyFailed = [...tests, ...manualTests].some(test => test.status === "failed");
  const totalDuration = tests.reduce((sum, test) => sum + (test.durationMs || 0), 0);
  const [elapsed, setElapsed] = useState(0);

  useEffect(() => {
    if (!current) {
      setElapsed(0);
      return;
    }
    const started = performance.now();
    setElapsed(0);
    const timer = window.setInterval(() => setElapsed(Math.round(performance.now() - started)), 500);
    return () => window.clearInterval(timer);
  }, [current?.id]);

  const phases = [
    { label: "设备通道", detail: connected ? "ADB 已建立" : "等待设备", state: connected ? "done" : "waiting" },
    { label: "自动采集", detail: current ? `${currentIndex + 1}/${tests.length} 执行中` : automaticFinished ? `${completed}/${tests.length} 已完成` : "等待启动", state: current ? "active" : automaticFinished ? "done" : "waiting" },
    { label: "人工确认", detail: manualActive ? "等待操作员" : manualFinished ? "已处理" : "尚未开始", state: manualActive ? "active" : manualFinished ? "done" : "waiting" },
    { label: "质量结论", detail: manualFinished ? anyFailed ? "发现异常" : "检测通过" : "尚未生成", state: manualFinished ? anyFailed ? "failed" : "done" : "waiting" }
  ];
  const protocol = current ? testProtocols[current.id] : undefined;
  const phaseProgress = manualFinished ? 100 : manualActive ? 74 : automaticFinished ? 58 : current ? 34 : connected ? 8 : 0;

  return (
    <section className={`run-monitor ${current ? "is-running" : automaticFinished ? "is-complete" : "is-idle"}`} aria-live="polite">
      <div className="process-track" style={{ "--phase-progress": phaseProgress / 100 } as CSSProperties}>
        {phases.map((phase, index) => (
          <div className={`process-stage stage-${phase.state}`} key={phase.label}>
            <span className="stage-marker">{phase.state === "done" ? <Check weight="bold" /> : phase.state === "failed" ? <X weight="bold" /> : index + 1}</span>
            <div><strong>{phase.label}</strong><small>{phase.detail}</small></div>
          </div>
        ))}
      </div>
      <div className="live-operation">
        <div className="live-signal">{current ? <CircleNotch className="status-spinner" weight="bold" /> : automaticFinished ? <Check weight="bold" /> : <Pulse weight="duotone" />}</div>
        <div className="live-copy">
          <span>{current ? `当前项目 ${String(currentIndex + 1).padStart(2, "0")}` : automaticFinished ? "自动检测完成" : "检测序列就绪"}</span>
          <strong>{current ? `正在采集 ${current.label}` : automaticFinished ? `${completed} 项自动检测已逐项完成` : "等待操作员开始硬件检测"}</strong>
          <p>{current ? protocol?.method : automaticFinished ? "每项真实测量值、耗时与判定结果均已记录。" : "启动后将严格按照左侧到右侧的顺序读取真机数据。"}</p>
        </div>
        <div className="live-source">
          <span>{current ? "真实数据源" : automaticFinished ? "自动采集耗时" : "执行模式"}</span>
          <code>{current ? protocol?.source : automaticFinished ? `${totalDuration} ms` : "顺序执行"}</code>
          {current && <strong>{(elapsed / 1000).toFixed(1)} s</strong>}
        </div>
        {current && <div className="live-scan-line" />}
      </div>
    </section>
  );
}

function TestDetail({ test, device }: { test: HardwareTestResult; device?: StationSnapshot["selectedDevice"] }) {
  const Icon = testIcons[test.id] || TestTube;
  const protocol = testProtocols[test.id];
  const metrics = visualMetrics(test);
  const parsedResult = test.id === "identity" && device
    ? `CPUID=${device.cpuid || "未读取"}\nFIRMWARE=${device.firmwareVersion || "未读取"}`
    : test.value || test.detail || (test.status === "running" ? "ADB 命令已下发，等待设备返回数据..." : "尚未产生结果");
  const command = test.command || protocol?.command || "该项目由操作员人工确认";
  const rawOutput = test.rawOutput
    || (test.status === "running" ? "等待设备 stdout / stderr…" : test.status === "waiting" ? "尚未执行，暂无设备返回。" : "命令执行成功，未产生标准输出。");
  const serial = device?.serial || "<device-serial>";
  return (
    <div className={`test-detail test-${test.status}`}>
      <div className="detail-header">
        <div className="detail-title">
          <span className="detail-icon"><Icon weight="duotone" /></span>
          <div><span>{test.group}</span><h3>{test.label}</h3></div>
        </div>
        <div className="detail-verdict"><TestStatus status={test.status} /><span>{test.durationMs !== undefined ? `${test.durationMs} ms` : "尚未执行"}</span></div>
      </div>

      <div className={`verdict-strip verdict-${test.status}`}>
        <Pulse weight="duotone" />
        <div><span>本次结论</span><strong>{test.detail || test.summary}</strong></div>
      </div>

      {metrics.length > 0 ? (
        <div className={`metric-grid metrics-${Math.min(metrics.length, 3)}`}>
          {metrics.map(metric => (
            <div className="metric" key={metric.label}>
              <span>{metric.label}</span>
              <strong>{metric.value}</strong>
              <small>{metric.note}</small>
              {metric.percent !== undefined && (
                <div className="metric-scale" aria-label={`${metric.label} ${metric.percent.toFixed(0)}%`}>
                  <span style={{ transform: `scaleX(${metric.percent / 100})` }} />
                </div>
              )}
            </div>
          ))}
        </div>
      ) : (
        <div className="metric-empty"><TestTube weight="duotone" /><span>{test.status === "waiting" ? "执行检测后显示真实测量值" : test.status === "running" ? "正在采集并解析真机返回值" : test.value || "本项目没有返回可视化数值"}</span></div>
      )}

      <div className="execution-evidence">
        <div className="evidence-head">
          <span><TerminalWindow weight="duotone" />执行证据</span>
          <small>{test.rawOutput ? "设备原始返回" : test.status === "running" ? "指令已下发" : "等待执行"}</small>
        </div>
        <div className="terminal-command">
          <span>$</span>
          <div><code>adb -s {serial} shell</code><pre>{command}</pre></div>
        </div>
        <div className={`terminal-output output-${test.status}`}>
          <span>原始输出</span>
          <pre>{rawOutput}</pre>
        </div>
        <div className="evidence-resolution">
          <div><span>解析结果</span><code>{parsedResult}</code></div>
          <div><span>判定依据</span><p>{protocol?.rule}</p></div>
        </div>
      </div>

      <div className="protocol-grid">
        <div><Code weight="duotone" /><span>测试方法</span><p>{protocol?.method}</p></div>
        <div><Database weight="duotone" /><span>真实数据源</span><code>{protocol?.source}</code></div>
        <div><Crosshair weight="duotone" /><span>证据状态</span><p>{test.rawOutput ? "已保存本次命令、原始返回、解析值和最终判定。" : "执行后保存原始返回，检测失败时同时保存 stderr。"}</p></div>
      </div>
    </div>
  );
}

function ManualTestRow({ test, disabled, onConfirm, onStimulus, stimulusLabel }: { test: HardwareTestResult; disabled: boolean; onConfirm: (id: string, passed: boolean) => void; onStimulus?: () => void; stimulusLabel?: string }) {
  const Icon = testIcons[test.id] || TestTube;
  const canConfirm = test.status !== "waiting" && test.status !== "running" && (test.id !== "microphone" || Boolean(test.rawOutput));
  const canPass = canConfirm && !(test.id === "microphone" && test.status === "failed");
  return (
    <article className={`manual-test test-${test.status}`}>
      <div className="manual-test-head">
        <span className="test-icon"><Icon weight="duotone" /></span>
        <div className="test-copy"><strong>{test.label}</strong><p>{test.detail || test.summary}</p></div>
        <TestStatus status={test.status} />
      </div>
      {test.value && <div className="manual-measurement"><span>实测电平</span><strong>{test.value}</strong></div>}
      {test.id === "microphone" && test.command && <div className="manual-evidence"><code>$ {test.command}</code>{test.rawOutput && <pre>{test.rawOutput}</pre>}</div>}
      {onStimulus && <Button size="small" appearance="subtle" disabled={disabled || test.status === "waiting" || test.status === "running"} onClick={onStimulus}>{stimulusLabel || "播放真实测试音"}</Button>}
      <div className="confirm-actions">
        <Button size="small" appearance={test.status === "failed" ? "primary" : "secondary"} className="fail-button" disabled={disabled || !canConfirm} onClick={() => onConfirm(test.id, false)}>有异常</Button>
        <Button size="small" appearance={test.status === "passed" ? "primary" : "secondary"} className="pass-button" disabled={disabled || !canPass} onClick={() => onConfirm(test.id, true)}>确认通过</Button>
      </div>
    </article>
  );
}

const testCodes: Record<string, string> = {
  identity: "ID",
  storage: "EMMC",
  memory: "RAM",
  wifi: "WIFI",
  bluetooth: "BT",
  environment: "AHT",
  power: "PWR",
  audio: "ALSA",
  "touch-controller": "TP",
  screen: "LCD",
  touch: "TOUCH",
  speaker: "SPK",
  microphone: "MIC"
};

const interactiveGuides: Record<string, { method: string; source: string; rule: string; steps: string[] }> = {
  screen: {
    method: "由操作员观察设备实体屏幕当前画面，检查面板显示质量。",
    source: "设备实体屏幕",
    rule: "当前为人工目视判定，不是 ADB 自动测量；画面应无坏点、异常色块、闪烁和边缘缺失。",
    steps: ["观察屏幕中央和四角是否存在亮点或暗点", "检查当前画面有无异常色块和偏色", "确认画面稳定且边缘显示完整"]
  },
  touch: {
    method: "在设备屏幕上执行点按和连续滑动，确认触控覆盖与轨迹连续性。",
    source: "gt9xxnew_ts 实体触摸面板",
    rule: "全屏区域均可响应，滑动轨迹连续且没有明显漂移。",
    steps: ["依次触摸屏幕四角与中央区域", "沿屏幕边缘连续滑动一圈", "确认没有断触、漂移或异常跳点"]
  },
  speaker: {
    method: "工具生成双频测试音并通过设备真实扬声器播放。",
    source: "default ALSA playback · audiocodec",
    rule: "测试音清晰、音量正常，没有破音、杂音或间歇中断。",
    steps: ["保持测试环境相对安静", "点击播放真实测试音", "确认两个频段均清晰且无杂音"]
  },
  microphone: {
    method: "通过与涂鸦相同的采集链路录制 3 秒，计算 RMS 和峰值后原音回放。",
    source: "default → CaptureDsnoop → hw:audiocodec,0",
    rule: "RMS 不低于 -50 dBFS，峰值不低于 -35 dBFS，回放人声清晰。",
    steps: ["靠近设备并以正常音量连续说话", "点击录音并原音回放", "核对电平后确认人声、底噪与失真"]
  }
};

function guideFor(test: HardwareTestResult) {
  const protocol = testProtocols[test.id];
  if (protocol) {
    return {
      method: protocol.method,
      source: protocol.source,
      rule: protocol.rule,
      steps: [protocol.method, `读取 ${protocol.source}`, `按照判定条件生成 ${statusLabels[test.status]} 结论`]
    };
  }
  return interactiveGuides[test.id] || {
    method: test.summary,
    source: "设备实体交互",
    rule: "由操作员根据实际现象确认。",
    steps: [test.summary]
  };
}

function TestRailNode({ test, index, selected, onSelect }: { test: HardwareTestResult; index: number; selected: boolean; onSelect: () => void }) {
  return (
    <button
      type="button"
      data-test-id={test.id}
      className={`rail-node test-${test.status} ${selected ? "active" : ""}`}
      onClick={onSelect}
      aria-label={`${String(index + 1).padStart(2, "0")} ${test.label}，${statusLabels[test.status]}`}
    >
      <span className="rail-node-top"><span>{String(index + 1).padStart(2, "0")} · {testCodes[test.id] || "TEST"}</span><i aria-hidden="true" /></span>
      <strong>{test.label}</strong>
      <small>{test.value || test.summary}</small>
    </button>
  );
}

function TestVisualization({ test, metrics }: { test: HardwareTestResult; metrics: VisualMetric[] }) {
  const isLive = test.status === "running";
  const hasMicrophoneSample = test.id === "microphone" && Boolean(test.rawOutput || test.value || test.durationMs);
  const seed = test.id.split("").reduce((sum, character) => sum + character.charCodeAt(0), 0);
  const wave = (count: number, offset = 0) => Array.from({ length: count }, (_, index) => {
    const variation = index * 137 + seed + offset;
    return (
      <i key={index} style={{
        "--signal-height": `${18 + ((index * 29 + seed + offset) % 76)}%`,
        "--signal-delay": `${index * -47}ms`,
        "--signal-speed": `${680 + (variation % 920)}ms`,
        "--signal-idle-speed": `${2200 + (variation % 1600)}ms`,
        "--signal-floor": `${0.24 + (variation % 25) / 100}`,
        "--signal-mid": `${0.56 + (variation % 31) / 100}`
      } as CSSProperties} />
    );
  });

  if (test.id === "identity") return (
    <div className="identity-visual"><IdentificationCard weight="duotone" /><div><span>设备身份链路</span><strong>{test.value || "等待读取 CPUID"}</strong><code>{test.rawOutput?.split("\n")[0] || "/sys/class/sunxi_info/sys_info"}</code></div></div>
  );

  if (test.id === "storage" || test.id === "memory") {
    const percent = metrics[0]?.percent ?? (test.status === "passed" ? 96 : 12);
    const active = Math.round(percent / 5);
    return (
      <div className="capacity-visual"><div className="capacity-readout"><span>{test.id === "storage" ? "eMMC 可用规格" : "系统可见内存"}</span><strong>{metrics[0]?.value || "等待容量采样"}</strong><small>{metrics[0]?.note || "读取设备真实容量"}</small></div><div className="capacity-blocks">{Array.from({ length: 20 }, (_, index) => <i className={index < active ? "active" : ""} key={index} />)}</div><div className="capacity-scale"><span>0</span><span>{percent.toFixed(0)}%</span><span>BOM</span></div></div>
    );
  }

  if (test.id === "wifi") {
    const apCount = metrics.find(metric => metric.label === "扫描结果");
    const signal = metrics.find(metric => metric.label === "最强信号");
    const state = metrics.find(metric => metric.label === "接口状态");
    const rawLines = test.rawOutput?.split("\n").map(line => line.trim()).filter(Boolean) || [];
    const mac = rawLines.find(line => /^([0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(line));
    return (
      <div className="wifi-scan-visual">
        <div className="wifi-scan-summary"><WifiHigh weight="duotone" /><span>本次 RF 扫描</span><strong>{apCount?.value || "等待扫描"}</strong><small>无需连接互联网</small></div>
        <div className="wifi-signal-readout">
          <header><span>最强接入点信号</span><strong>{signal?.value || "暂无数据"}</strong></header>
          <div className={`wifi-signal-ruler ${signal ? "has-value" : ""}`}><i style={{ "--signal-position": `${signal?.percent ?? 0}%` } as CSSProperties} /></div>
          <div className="wifi-signal-scale"><span>-100 dBm</span><span>-65 dBm</span><span>-30 dBm</span></div>
        </div>
        <div className="wifi-scan-facts"><div><span>网络接口</span><strong>wlan0</strong></div><div><span>接口状态</span><strong>{state?.value || "等待返回"}</strong></div><div><span>硬件地址</span><strong>{mac || "等待返回"}</strong></div></div>
      </div>
    );
  }

  if (test.id === "bluetooth") return (
    <div className={`radar-visual ${isLive ? "is-live" : ""}`}><div className="radar-scope"><i /><i /><i /><span className="radar-sweep" /><b className="radar-target target-a" /><b className="radar-target target-b" /><b className="radar-target target-c" /></div><div><span>hci0 扫描状态</span><strong>{metrics[0]?.value || test.value || "等待控制器响应"}</strong><small>真实适配器 · UP RUNNING</small></div></div>
  );

  if (test.id === "environment" || test.id === "power") return (
    <div className="sensor-visual">{(metrics.length ? metrics : [{ label: "传感器", value: "等待采样", note: test.summary }]).map((metric, index) => <div key={metric.label}><span>{metric.label}</span><strong>{metric.value}</strong><small>{metric.note}</small><i><b style={{ transform: `scaleX(${(metric.percent ?? 36 + index * 17) / 100})` }} /></i></div>)}</div>
  );

  if (test.id === "screen") return (
    <div className="screen-inspection-visual">
      <header><Monitor weight="duotone" /><div><span>检测方式</span><strong>观察设备实体屏幕</strong></div><em>{test.status === "passed" ? "人工确认通过" : test.status === "failed" ? "人工判定异常" : "等待操作员判定"}</em></header>
      <div className="screen-checks">
        {[
          ["像素缺陷", "亮点、暗点", Crosshair],
          ["色彩异常", "色块、偏色", Monitor],
          ["刷新稳定", "闪烁、抖动", Pulse],
          ["边缘完整", "缺失、亮线", Check]
        ].map(([label, detail, Icon]) => (
          <div key={label as string} className={test.status === "passed" ? "confirmed" : test.status === "failed" ? "rejected" : "pending"}>
            <Icon weight="duotone" />
            <span>{label as string}</span>
            <small>{detail as string}</small>
          </div>
        ))}
      </div>
      <p>判定依据来自操作员目视观察，不生成虚假的桌面端屏幕数据。</p>
    </div>
  );

  if (test.id === "touch-controller") return (
    <div className="input-route-visual">
      <div className="input-route-track">
        <div><TerminalWindow weight="duotone" /><span>内核输入表</span><strong>/proc/bus/input/devices</strong></div>
        <i aria-hidden="true" />
        <div><Cpu weight="duotone" /><span>匹配驱动</span><strong>gt9xxnew_ts</strong></div>
        <i aria-hidden="true" />
        <div><HandTap weight="duotone" /><span>解析事件节点</span><strong>{test.value || "等待 event 节点"}</strong></div>
      </div>
      <p><span>真实检测结果</span><strong>{test.value || test.summary}</strong></p>
    </div>
  );

  if (test.id === "touch") return (
    <div className={`touch-visual ${isLive ? "is-live" : ""}`}><div className="touch-grid">{Array.from({ length: 9 }, (_, index) => <i key={index} className={test.status === "passed" || (isLive && index < 4) ? "hit" : ""}><span>{index + 1}</span></i>)}</div><div><span>全屏触控覆盖</span><strong>{test.status === "needsConfirmation" ? "等待操作员确认" : test.summary}</strong><small>四角、中心、边缘滑动均需有效</small></div></div>
  );

  if (test.id === "audio") return (
    <div className="audio-route-visual">
      <div className="audio-route-track">
        <div><TerminalWindow weight="duotone" /><span>数据源</span><strong>/proc/asound/cards</strong></div>
        <i aria-hidden="true" />
        <div><SpeakerHigh weight="duotone" /><span>内核声卡</span><strong>{metrics[0]?.value || test.value || "等待返回"}</strong></div>
        <i aria-hidden="true" />
        <div><Check weight="bold" /><span>识别判定</span><strong>{test.status === "passed" ? "REGISTERED" : statusLabels[test.status].toUpperCase()}</strong></div>
      </div>
      <p><span>ALSA 原始返回</span><strong>{test.rawOutput?.split("\n").filter(Boolean).slice(0, 2).join("  ") || "等待读取声卡列表"}</strong></p>
    </div>
  );

  if (test.id === "speaker") return (
    <div className="speaker-test-visual">
      <SpeakerHigh weight="duotone" />
      <div><span>测试音序列</span><strong>660 Hz</strong><small>低频基准音</small></div>
      <i aria-hidden="true" />
      <div><span>第二频段</span><strong>880 Hz</strong><small>高频基准音</small></div>
      <p>{test.status === "needsConfirmation" ? "播放后由操作员确认清晰度、杂音和破音" : test.summary}</p>
    </div>
  );

  return (
    <div className={`microphone-visual ${isLive ? "is-recording" : hasMicrophoneSample ? "has-sample" : "is-idle"}`}>
      <div className={`signal-bars ${isLive ? "is-live" : ""}`}>{wave(34)}</div>
      <div className="visual-readouts">
        {metrics.length
          ? metrics.map(metric => <div key={metric.label}><span>{metric.label}</span><strong>{metric.value}</strong></div>)
          : <div className={isLive ? "microphone-live-readout" : undefined}>
              <span>{isLive && <i aria-hidden="true" />}采集状态</span>
              <strong>{isLive ? "请持续说话" : test.status === "needsConfirmation" ? "等待录音" : statusLabels[test.status]}</strong>
            </div>}
      </div>
    </div>
  );
}

function InstrumentStage({ test, device }: { test: HardwareTestResult; device?: StationSnapshot["selectedDevice"] }) {
  const metrics = visualMetrics(test);
  const guide = guideFor(test);
  const primaryValue = metrics[0]?.value || test.value || (test.status === "running" ? "采集中" : statusLabels[test.status]);
  const primaryLabel = metrics[0]?.label || (test.kind === "automatic" ? "当前结果" : "操作状态");
  const command = test.command || testProtocols[test.id]?.command || "本项目由操作员观察实体设备";
  const output = test.rawOutput || (test.status === "running" ? "等待设备 stdout / stderr…" : test.status === "waiting" ? "尚未执行，暂无设备返回。" : test.detail || test.summary);
  return (
    <section className={`instrument-stage instrument-${test.status}`} aria-live="polite">
      <div className="instrument-head">
        <div><span className="instrument-kicker">{statusLabels[test.status]} · 当前 ADB 设备</span><h2>{test.label}</h2></div>
        <div className="instrument-reading"><span>{primaryLabel}</span><strong>{primaryValue}</strong><small>{metrics[0]?.note || test.summary}</small></div>
      </div>
      <div className="instrument-body">
        <div className="signal-canvas" aria-hidden="true">
          <div className="signal-grid" />
          <TestVisualization test={test} metrics={metrics} />
          <div className="signal-caption"><span>{guide.source}</span><span>{test.durationMs !== undefined ? `${test.durationMs} ms` : "等待执行"}</span></div>
        </div>
        <section className="instrument-evidence">
          <div className="instrument-evidence-head"><strong>执行证据</strong><span>{test.rawOutput ? "真机返回" : "命令模板"}</span></div>
          <div className="instrument-command"><span>$</span><code>{command}</code></div>
          <pre>{output}</pre>
          {metrics.length > 0 && <div className="instrument-metrics">{metrics.slice(0, 3).map(metric => <div key={metric.label}><span>{metric.label}</span><strong>{metric.value}</strong></div>)}</div>}
        </section>
      </div>
      <div className="instrument-foot"><span>{device?.firmwareVersion || "固件未读取"}</span><code>{device?.serial || "等待 ADB 设备"}</code><span>{test.detail || guide.rule}</span></div>
    </section>
  );
}

function InspectorPanel({ test, index, total, disabled, hardwareStatus, confirmations, onSelectConfirmation, onConfirm, onStimulus, onRetest, onReset }: {
  test: HardwareTestResult;
  index: number;
  total: number;
  disabled: boolean;
  hardwareStatus: StationSnapshot["hardwareStatus"];
  confirmations: HardwareTestResult[];
  onSelectConfirmation: (id: string) => void;
  onConfirm: (id: string, passed: boolean) => void;
  onStimulus?: () => void;
  onRetest?: () => void;
  onReset: () => void;
}) {
  const guide = guideFor(test);
  const metrics = visualMetrics(test);
  const isResolved = test.status === "passed" || test.status === "failed";
  const canConfirm = test.status === "needsConfirmation" && (test.id !== "microphone" || Boolean(test.rawOutput));
  const canPass = canConfirm && !(test.id === "microphone" && test.status === "failed");
  const stimulusLabel = test.id === "microphone" ? "录音并原音回放（3 秒）" : "播放真实测试音";
  return (
    <aside className="inspector-panel">
      <div className="inspector-heading"><h2>判定与操作</h2><TestStatus status={test.status} /></div>
      {confirmations.length > 0 && (
        <section className="confirmation-queue">
          <div><span>待确认队列</span><strong>{confirmations.length} 项</strong></div>
          <nav aria-label="待人工确认项目">{confirmations.map(item => (
            <button type="button" key={item.id} className={item.id === test.id ? "active" : ""} onClick={() => onSelectConfirmation(item.id)}><span>{testCodes[item.id]}</span><strong>{item.label}</strong><CaretRight weight="bold" /></button>
          ))}</nav>
        </section>
      )}
      <section className={`inspector-card test-${test.status}`}>
        <span className="inspector-index">步骤 {String(index + 1).padStart(2, "0")} / {String(total).padStart(2, "0")}</span>
        <h3>{test.label}</h3>
        <p>{guide.method}</p>
        <div className="inspector-metric"><span>{metrics[0]?.label || "本次状态"}</span><strong>{metrics[0]?.value || test.value || statusLabels[test.status]}</strong><small>{metrics[0]?.note || test.summary}</small></div>
        <div className="inspector-instructions"><h4>操作说明</h4><ol>{guide.steps.map(step => <li key={step}>{step}</li>)}</ol></div>
        <div className="inspector-criteria"><h4>判定条件</h4><p>{guide.rule}</p><div><span>真实数据源</span><code>{guide.source}</code></div></div>
        {test.kind === "interactive" && isResolved ? (
          <div className="inspection-result">
            <div className={`inspection-complete ${test.status}`}>
              {test.status === "passed" ? <Check weight="bold" /> : <X weight="bold" />}
              <div><strong>{test.status === "passed" ? "本项判定已完成" : "本项已记录异常"}</strong><span>{test.status === "passed" ? "操作员已确认测试结果，无需继续操作。" : "异常结果已写入本轮检测记录。"}</span></div>
            </div>
            {test.status === "failed" && test.id === "microphone" && onRetest && (
              <div className="retest-panel">
                <p>测试员未讲话、距离过远或环境太安静都可能导致电平过低。请靠近设备正常说话后，仅重新采集本项。</p>
                <Button appearance="primary" icon={<ArrowClockwise weight="bold" />} disabled={disabled} onClick={onRetest}>重新录音测试本项</Button>
              </div>
            )}
          </div>
        ) : test.kind === "interactive" ? (
          <div className="inspector-actions">
            {onStimulus && <Button appearance="secondary" disabled={disabled || test.status === "waiting" || test.status === "running"} onClick={onStimulus}>{stimulusLabel}</Button>}
            <div><Button appearance="secondary" disabled={disabled || !canConfirm} onClick={() => onConfirm(test.id, false)}>判定异常</Button><Button appearance="primary" disabled={disabled || !canPass} onClick={() => onConfirm(test.id, true)}>确认通过</Button></div>
          </div>
        ) : <div className="automatic-note"><Pulse weight="duotone" /><span>此项目由自动流程执行，结果只取自当前 ADB 设备。</span></div>}
      </section>
      {hardwareStatus !== "idle" && <Button className="inspector-reset" appearance="subtle" disabled={disabled} onClick={onReset}>清除本轮结果</Button>}
    </aside>
  );
}

function EventConsole({ events }: { events: EventRecord[] }) {
  return (
    <section className="event-console">
      <div className="console-head"><strong>实时检测日志</strong><span>本机事件 · {events.length} 条</span></div>
      <div className="console-list" role="log" aria-live="polite" aria-relevant="additions text">{events.length ? events.map(event => (
        <div className={`console-row ${event.level}`} key={`${event.timestamp}-${event.message}`}><time>{event.timestamp}</time><span><EventIcon level={event.level} />{event.level === "success" ? "通过" : event.level === "error" ? "异常" : event.level === "warning" ? "注意" : "信息"}</span><p>{event.message}</p></div>
      )) : <p className="console-empty">当前没有检测记录</p>}</div>
    </section>
  );
}

function App() {
  const [snapshot, setSnapshot] = useState<StationSnapshot>();
  const [selectedTestId, setSelectedTestId] = useState("identity");
  const [adminOpen, setAdminOpen] = useState(false);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string>();
  const previousHardwareStatus = useRef<StationSnapshot["hardwareStatus"] | undefined>(undefined);

  useEffect(() => {
    let active = true;
    let unsubscribe: (() => void) | undefined;
    getSnapshot().then(value => active && setSnapshot(value)).catch(reason => active && setError(String(reason)));
    onSnapshot(value => active && setSnapshot(value)).then(fn => { unsubscribe = fn; });
    return () => { active = false; unsubscribe?.(); };
  }, []);

  const runningTestId = snapshot?.hardwareTests.find(test => test.status === "running")?.id;
  useEffect(() => {
    if (runningTestId) setSelectedTestId(runningTestId);
  }, [runningTestId]);

  const firstConfirmationId = snapshot?.hardwareTests.find(test => test.kind === "interactive" && test.status === "needsConfirmation")?.id;
  useEffect(() => {
    const previous = previousHardwareStatus.current;
    const current = snapshot?.hardwareStatus;
    previousHardwareStatus.current = current;
    if (!current || current === "running" || !firstConfirmationId) return;
    if (previous === undefined || previous === "running") setSelectedTestId(firstConfirmationId);
  }, [snapshot?.hardwareStatus, firstConfirmationId]);

  const execute = useCallback(async (action: () => Promise<StationSnapshot>) => {
    setPending(true);
    setError(undefined);
    try { setSnapshot(await action()); }
    catch (reason) { setError(String(reason).replace(/^Error:\s*/, "")); }
    finally { setPending(false); }
  }, []);

  const recentEvents = useMemo(() => snapshot?.events.slice(0, 20) ?? [], [snapshot?.events]);

  useEffect(() => {
    const target = document.querySelector<HTMLElement>(`[data-test-id="${selectedTestId}"]`);
    if (!target) return;
    const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    target.scrollIntoView({ behavior: reducedMotion ? "auto" : "smooth", block: "nearest", inline: "center" });
  }, [selectedTestId]);

  if (!snapshot) {
    return error ? (
      <main className="boot-screen boot-error">
        <div className="boot-mark"><X weight="bold" /></div>
        <div className="boot-copy"><strong>本地检测服务连接失败</strong><span>{error.replace(/^Error:\s*/, "")}</span></div>
        <Button appearance="primary" icon={<ArrowClockwise weight="bold" />} disabled={pending} onClick={() => execute(getSnapshot)}>重新连接</Button>
      </main>
    ) : (
      <main className="boot-screen" aria-busy="true"><div className="boot-mark"><HardDrives weight="duotone" /></div><div className="boot-copy"><strong>正在连接本地检测服务</strong><span>扫描 ADB 与加载硬件规则</span></div><div className="boot-line" /></main>
    );
  }

  const activeDevice = snapshot.selectedDevice;
  const allTests = snapshot.hardwareTests;
  const confirmationTests = allTests.filter(test => test.kind === "interactive" && test.status === "needsConfirmation");
  const selectedTest = allTests.find(test => test.id === selectedTestId) || allTests[0];
  const selectedIndex = Math.max(0, allTests.findIndex(test => test.id === selectedTest?.id));
  const passedCount = snapshot.hardwareTests.filter(test => test.status === "passed").length;
  const failedCount = snapshot.hardwareTests.filter(test => test.status === "failed").length;
  const confirmCount = snapshot.hardwareTests.filter(test => test.status === "needsConfirmation").length;
  const pendingCount = snapshot.hardwareTests.length - passedCount - failedCount;
  const isPassed = snapshot.hardwareStatus === "passed";
  const stationFailed = snapshot.phase === "failed" && snapshot.hardwareStatus === "idle";
  const isFailed = snapshot.hardwareStatus === "failed" || stationFailed || Boolean(error);
  const isWorking = snapshot.hardwareStatus === "running" || snapshot.busy;
  const needsConfirmation = snapshot.hardwareStatus === "needsConfirmation";
  const hasRunningTest = allTests.some(test => test.status === "running");
  const runnable = Boolean(activeDevice && !snapshot.busy && !pending && !hasRunningTest);
  const statusTitle = stationFailed ? "设备识别失败" : isPassed ? "本机硬件检测通过" : isFailed ? "硬件检测发现异常" : needsConfirmation ? "自动检测完成" : isWorking ? "正在读取真机数据" : activeDevice ? "设备已连接，可以开始检测" : "等待设备接入";
  const stimulus = selectedTest?.id === "speaker" && activeDevice
    ? () => execute(() => playHardwareTestTone(activeDevice.serial))
    : selectedTest?.id === "microphone" && activeDevice
      ? () => execute(() => runMicrophoneHardwareTest(activeDevice.serial))
      : undefined;

  return (
    <div className="factory-shell">
      <header className="topbar">
        <div className="brand-lockup"><span className="brand-symbol"><HardDrives weight="duotone" /></span><div><strong>AITVBOX 出厂助手</strong><span>硬件品质实验室</span></div></div>
        <div className="header-station"><span>当前工位</span><strong>{snapshot.stationName}</strong></div>
        <div className="station-strip">
          <div className={`connection-state ${activeDevice ? "local-mode" : "is-offline"}`}><Pulse weight="fill" />{activeDevice ? "ADB 实时链路" : "等待设备"}</div>
          <Button appearance="secondary" icon={<ArrowClockwise size={16} />} disabled={isWorking || pending} onClick={() => execute(scanDevices)}>重新扫描</Button>
          <Button appearance="primary" icon={isWorking ? <CircleNotch className="status-spinner" weight="bold" /> : <Play weight="fill" />} disabled={!runnable} onClick={() => activeDevice && execute(() => startHardwareTests(activeDevice.serial))}>{isWorking ? "正在检测" : snapshot.hardwareStatus === "idle" ? "开始硬件检测" : "重新检测"}</Button>
          <Button appearance="subtle" icon={<GearSix size={20} />} className="icon-button" aria-label="检测规则" title="检测规则" onClick={() => setAdminOpen(true)} />
        </div>
      </header>

      <main className="workspace">
        <section className="console-main">
          <div className="console-title-row">
            <div><span>标准出厂测试 · 本地真实数据</span><h1>硬件功能测试流程</h1><p>{error || (activeDevice ? snapshot.statusMessage : "插入一台已完成全志烧录的设备，系统将自动读取身份。")}</p></div>
            <div className="result-summary" aria-label="测试结果摘要"><div><strong>{passedCount}</strong><span>通过</span></div><div><strong>{failedCount}</strong><span>异常</span></div><div><strong>{pendingCount}</strong><span>待处理</span></div></div>
          </div>
          <div className="overall-progress"><span style={{ transform: `scaleX(${snapshot.hardwareProgress / 100})` }} /></div>

          <section className={`device-strip ${activeDevice ? "is-connected" : ""}`}>
            <div className="device-identity"><span className="device-avatar">{activeDevice ? "A1" : <Usb weight="duotone" />}</span><div><strong>{statusTitle}</strong><small>{activeDevice?.serial || "等待 ADB 设备接入"}</small></div></div>
            <div className="strip-facts"><div><span>固件版本</span><strong>{activeDevice?.firmwareVersion || "尚未读取"}</strong></div><div><span>CPUID 末 12 位</span><strong>{activeDevice?.cpuid?.slice(-12).toUpperCase() || "尚未读取"}</strong></div><div><span>数据通道</span><strong>USB · 本地 ADB</strong></div><div><span>检测进度</span><strong>{snapshot.hardwareProgress}%</strong></div></div>
            <button type="button" className={`device-verdict state-${snapshot.hardwareStatus}`} disabled={!confirmationTests.length} onClick={() => confirmationTests[0] && setSelectedTestId(confirmationTests[0].id)}>{isWorking ? "检测中" : isPassed ? "全部通过" : isFailed ? "发现异常" : needsConfirmation ? `${confirmCount} 项待确认` : activeDevice ? "设备就绪" : "未连接"}</button>
          </section>

          <section className="test-sequence">
            <div className="sequence-head"><strong>测试序列</strong><span>{allTests.length} 项 · 点击查看证据</span></div>
            <nav className="test-rail" aria-label="硬件测试序列">{allTests.map((test, index) => <TestRailNode key={test.id} test={test} index={index} selected={selectedTest?.id === test.id} onSelect={() => setSelectedTestId(test.id)} />)}</nav>
          </section>

          {selectedTest && <InstrumentStage key={`${selectedTest.id}-${selectedTest.status}`} test={selectedTest} device={activeDevice} />}
          <EventConsole events={recentEvents} />
        </section>

        {selectedTest && <InspectorPanel test={selectedTest} index={selectedIndex} total={allTests.length} disabled={pending || snapshot.busy || selectedTest.status === "running"} hardwareStatus={snapshot.hardwareStatus} confirmations={confirmationTests} onSelectConfirmation={setSelectedTestId} onConfirm={(id, passed) => execute(async () => {
          const nextSnapshot = await confirmHardwareTest(id, passed);
          const nextConfirmation = nextSnapshot.hardwareTests.find(item => item.kind === "interactive" && item.status === "needsConfirmation");
          setSelectedTestId(nextConfirmation?.id || id);
          return nextSnapshot;
        })} onStimulus={stimulus} onRetest={selectedTest.id === "microphone" ? stimulus : undefined} onReset={() => execute(resetRun)} />}
      </main>

      <Dialog open={adminOpen} onOpenChange={(_, data) => setAdminOpen(data.open)}><DialogSurface className="admin-dialog"><DialogBody><DialogTitle>本地硬件检测规则</DialogTitle><DialogContent><p className="dialog-intro">自动检测由 Rust 核心通过 ADB 读取真机；规则可通过工位环境变量覆盖。</p><div className="rule-grid"><div><strong>eMMC 容量</strong><span>默认 BOM：16 GB，容差 5%</span></div><div><strong>运行内存</strong><span>默认 BOM：2 GB，容差 10%</span></div><div><strong>无线射频</strong><span>有效 MAC 且扫描到 AP</span></div><div><strong>传感器</strong><span>AHT20 与 INA219 数据完整</span></div></div><div className="host-note"><Cpu weight="duotone" /><div><span>当前运行环境</span><strong>{snapshot.hostPlatform}</strong></div></div></DialogContent><DialogActions><Button appearance="primary" onClick={() => setAdminOpen(false)}>完成</Button></DialogActions></DialogBody></DialogSurface></Dialog>
    </div>
  );
}

export default App;
