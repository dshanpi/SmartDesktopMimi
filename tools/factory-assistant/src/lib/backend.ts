import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import type { HardwareTestResult, StationSnapshot } from "../types";

const isTauri = () => "__TAURI_INTERNALS__" in window;

const now = () =>
  new Intl.DateTimeFormat("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false
  }).format(new Date());

let browserSnapshot: StationSnapshot = {
  hostPlatform: "Ubuntu 本地 ADB",
  stationName: "开发工位",
  batchNumber: "未配置",
  adbAvailable: false,
  cloudConfigured: false,
  busy: false,
  phase: "idle",
  progress: 0,
  statusMessage: "等待设备接入",
  devices: [],
  configChecks: [
    { name: "STATION_ID", configured: true, sensitive: false },
    { name: "BATCH_NUMBER", configured: true, sensitive: false },
    { name: "TUYA_PID", configured: true, sensitive: false },
    { name: "SIGNING_PRIVATE_KEY_PATH", configured: false, sensitive: true },
    { name: "FACTORY_DATA_DIR", configured: true, sensitive: false }
  ],
  licenseInventory: { available: 0, reserved: 0, written: 0, shipped: 0, total: 0 },
  tuyaPid: "alon7qgyjj8yus74",
  factoryHome: "",
  licenseDbPath: "",
  hardwareStatus: "idle",
  hardwareProgress: 0,
  hardwareTests: [
    ["identity", "设备身份", "基础", "校验 CPUID 与固件版本"],
    ["storage", "存储容量", "基础", "读取 eMMC 实际容量"],
    ["memory", "运行内存", "基础", "读取系统可用内存规格"],
    ["wifi", "Wi-Fi 模组", "无线", "校验 wlan0、硬件地址与 RF 扫描"],
    ["bluetooth", "蓝牙控制器", "无线", "校验 hci0 工作状态"],
    ["environment", "温湿度传感器", "传感", "读取 AHT20 实测数据"],
    ["power", "电源监测", "传感", "读取 INA219 电压电流"],
    ["audio", "音频设备", "多媒体", "校验 ALSA 播放设备"],
    ["touch-controller", "触控控制器", "交互", "识别 gt9xx 输入节点"]
  ].map(([id, label, group, summary]) => ({
    id,
    label,
    group,
    summary,
    kind: "automatic",
    status: "waiting"
  } as HardwareTestResult)).concat([
    { id: "screen", label: "屏幕显示", group: "人工确认", kind: "interactive", status: "waiting", summary: "检查亮点、暗点与色彩" },
    { id: "touch", label: "触摸响应", group: "人工确认", kind: "interactive", status: "waiting", summary: "确认全屏触控连续有效" },
    { id: "speaker", label: "扬声器", group: "人工确认", kind: "interactive", status: "waiting", summary: "确认测试音清晰无杂音" },
    { id: "microphone", label: "麦克风", group: "人工确认", kind: "interactive", status: "waiting", summary: "说话 3 秒，检测真实输入电平并原音回放", command: "arecord -D default -t wav -f S16_LE -r 16000 -c 1 -d 3 /tmp/aitvbox_factory_mic.wav" }
  ]),
  events: [
    {
      timestamp: now(),
      level: "info",
      message: "本地 ADB 检测服务已启动"
    }
  ]
};
const browserListeners = new Set<(snapshot: StationSnapshot) => void>();

const clone = () => structuredClone(browserSnapshot);
const publishBrowser = () => {
  const snapshot = clone();
  browserListeners.forEach(listener => listener(snapshot));
};

async function localApi<T>(path: string, body: Record<string, unknown> = {}): Promise<T> {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), 35_000);
  try {
    const response = await fetch(`/api/factory/${path}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
      signal: controller.signal
    });
    const payload = await response.json().catch(() => ({})) as { error?: string } & T;
    if (!response.ok) throw new Error(payload.error || "本地 ADB 检测服务调用失败");
    return payload;
  } catch (error) {
    if (error instanceof DOMException && error.name === "AbortError") {
      throw new Error("本地 ADB 检测服务响应超时");
    }
    throw error;
  } finally {
    window.clearTimeout(timeout);
  }
}

export async function getSnapshot(): Promise<StationSnapshot> {
  return isTauri() ? invoke("get_station_snapshot") : scanDevices();
}

export async function scanDevices(): Promise<StationSnapshot> {
  if (isTauri()) return invoke("scan_devices");
  const result = await localApi<{
    adbAvailable: boolean;
    devices: StationSnapshot["devices"];
    selectedDevice?: StationSnapshot["selectedDevice"];
    message: string;
    error?: string;
  }>("scan");
  browserSnapshot = {
    ...browserSnapshot,
    adbAvailable: result.adbAvailable,
    devices: result.devices,
    selectedDevice: result.selectedDevice,
    phase: result.error && result.devices.length > 0 ? "failed" : "idle",
    statusMessage: result.message,
    hardwareStatus: "idle",
    hardwareProgress: 0,
    hardwareTests: browserSnapshot.hardwareTests.map(test => ({
      ...test,
      status: "waiting",
      value: undefined,
      detail: undefined,
      rawOutput: undefined,
      durationMs: undefined
    })),
    events: [
      {
        timestamp: now(),
        level: result.error ? "warning" : "success",
        message: result.error || "真机设备身份已读取"
      },
      ...browserSnapshot.events
    ]
  };
  publishBrowser();
  return clone();
}

export async function startProduction(serial: string): Promise<StationSnapshot> {
  if (!isTauri()) throw new Error("浏览器预览不能执行真实生产流程");
  return invoke("start_production_run", { serial });
}

export async function startHardwareTests(serial: string): Promise<StationSnapshot> {
  if (isTauri()) return invoke("start_hardware_tests", { serial });
  if (browserSnapshot.selectedDevice?.serial !== serial) throw new Error("请先重新检测设备");

  browserSnapshot = {
    ...browserSnapshot,
    busy: true,
    phase: "hardwareTest",
    progress: 0,
    hardwareStatus: "running",
    hardwareProgress: 0,
    statusMessage: "正在执行自动硬件检测",
    hardwareTests: browserSnapshot.hardwareTests.map(test => ({
      ...test,
      status: "waiting",
      value: undefined,
      detail: undefined,
      rawOutput: undefined,
      durationMs: undefined
    })),
    events: [{ timestamp: now(), level: "info", message: "硬件检测已开始" }, ...browserSnapshot.events]
  };
  publishBrowser();

  try {
    const automaticIds = browserSnapshot.hardwareTests
      .filter(test => test.kind === "automatic")
      .map(test => test.id);
    const results: HardwareTestResult[] = [];

    for (const [index, id] of automaticIds.entries()) {
      const target = browserSnapshot.hardwareTests.find(test => test.id === id);
      const label = target?.label || id;
      browserSnapshot = {
        ...browserSnapshot,
        statusMessage: `正在检测 ${index + 1}/${automaticIds.length}：${label}`,
        hardwareTests: browserSnapshot.hardwareTests.map(test => test.id === id
          ? { ...test, status: "running", summary: "正在建立 ADB 读取并采集真机数据" }
          : test),
        events: [{
          timestamp: now(),
          level: "info",
          message: `[${index + 1}/${automaticIds.length}] 开始检测 ${label}`
        }, ...browserSnapshot.events]
      };
      publishBrowser();

      const result = await localApi<HardwareTestResult>("hardware-test", { serial, id });
      results.push(result);
      const completed = index + 1;
      const resolvedProgress = Math.round((completed / browserSnapshot.hardwareTests.length) * 100);
      browserSnapshot = {
        ...browserSnapshot,
        hardwareProgress: resolvedProgress,
        progress: resolvedProgress,
        statusMessage: `${label}检测${result.status === "passed" ? "通过" : "异常"}，准备下一项`,
        hardwareTests: browserSnapshot.hardwareTests.map(test => test.id === id ? result : test),
        events: [{
          timestamp: now(),
          level: result.status === "passed" ? "success" : "error",
          message: `${label}：${result.summary} · ${result.durationMs ?? 0} ms`
        }, ...browserSnapshot.events]
      };
      publishBrowser();
    }

    const failed = results.filter(test => test.status === "failed").length;
    const manual = browserSnapshot.hardwareTests
      .filter(test => test.kind === "interactive")
      .map(test => ({ ...test, status: "needsConfirmation" as const }));
    const hardwareTests = [...results, ...manual];
    const progress = Math.round((results.length / hardwareTests.length) * 100);
    browserSnapshot = {
      ...browserSnapshot,
      busy: false,
      hardwareTests,
      hardwareProgress: progress,
      progress,
      hardwareStatus: failed > 0 ? "failed" : "needsConfirmation",
      statusMessage: failed > 0
        ? `真机自动检测发现 ${failed} 项异常，仍可完成人工确认`
        : `真机自动检测通过，等待 ${manual.length} 项人工确认`,
      events: [{
        timestamp: now(),
        level: failed > 0 ? "error" : "success",
        message: failed > 0
          ? `真机自动检测发现 ${failed} 项异常`
          : `${results.length} 项真机自动检测全部通过`
      }, ...browserSnapshot.events]
    };
    publishBrowser();
    return clone();
  } catch (reason) {
    const message = reason instanceof Error ? reason.message : String(reason);
    browserSnapshot = {
      ...browserSnapshot,
      busy: false,
      hardwareStatus: "failed",
      statusMessage: "真机硬件检测失败",
      events: [{ timestamp: now(), level: "error", message }, ...browserSnapshot.events]
    };
    publishBrowser();
    throw reason;
  }
}

export async function confirmHardwareTest(id: string, passed: boolean): Promise<StationSnapshot> {
  if (isTauri()) return invoke("confirm_hardware_test", { id, passed });
  const target = browserSnapshot.hardwareTests.find(test => test.id === id);
  if (!target || target.kind !== "interactive") throw new Error("人工检测项目无效");
  const hardwareTests = browserSnapshot.hardwareTests.map(test =>
    test.id === id
      ? { ...test, status: passed ? "passed" as const : "failed" as const, summary: passed ? "工人确认通过" : "工人确认异常", value: passed ? "已确认" : "不通过" }
      : test
  );
  const resolved = hardwareTests.filter(test => test.status === "passed" || test.status === "failed").length;
  const anyFailed = hardwareTests.some(test => test.status === "failed");
  const allResolved = hardwareTests.every(test => test.status === "passed" || test.status === "failed");
  browserSnapshot = {
    ...browserSnapshot,
    hardwareTests,
    hardwareProgress: Math.round((resolved / hardwareTests.length) * 100),
    progress: Math.round((resolved / hardwareTests.length) * 100),
    hardwareStatus: allResolved ? (anyFailed ? "failed" : "passed") : "needsConfirmation",
    statusMessage: allResolved ? (anyFailed ? "硬件检测存在异常" : "硬件检测全部通过") : "等待完成其余人工确认",
    events: [{ timestamp: now(), level: passed ? "success" : "error", message: `${target.label}：${passed ? "人工确认通过" : "人工确认异常"}` }, ...browserSnapshot.events]
  };
  publishBrowser();
  return clone();
}

export async function playHardwareTestTone(serial: string): Promise<StationSnapshot> {
  if (isTauri()) return invoke("play_hardware_test_tone", { serial });
  if (browserSnapshot.selectedDevice?.serial !== serial) throw new Error("请先重新检测设备");
  browserSnapshot = {
    ...browserSnapshot,
    hardwareTests: browserSnapshot.hardwareTests.map(test =>
      test.id === "speaker" ? { ...test, summary: "正在向设备播放双音测试音" } : test
    ),
    events: [{ timestamp: now(), level: "info", message: "正在播放扬声器测试音" }, ...browserSnapshot.events]
  };
  publishBrowser();
  try {
    await localApi<{ ok: boolean }>("tone", { serial });
    browserSnapshot = {
      ...browserSnapshot,
      hardwareTests: browserSnapshot.hardwareTests.map(test =>
        test.id === "speaker" ? { ...test, summary: "测试音已播放，请确认是否清晰无杂音", detail: undefined } : test
      ),
      events: [{ timestamp: now(), level: "info", message: "扬声器测试音播放完成，等待人工确认" }, ...browserSnapshot.events]
    };
    publishBrowser();
    return clone();
  } catch (reason) {
    const message = reason instanceof Error ? reason.message : String(reason);
    browserSnapshot = {
      ...browserSnapshot,
      hardwareTests: browserSnapshot.hardwareTests.map(test =>
        test.id === "speaker" ? { ...test, summary: "测试音播放失败", detail: message } : test
      ),
      events: [{ timestamp: now(), level: "error", message }, ...browserSnapshot.events]
    };
    publishBrowser();
    throw reason;
  }
}

export async function runMicrophoneHardwareTest(serial: string): Promise<StationSnapshot> {
  if (isTauri()) return invoke("run_microphone_hardware_test", { serial });
  if (browserSnapshot.selectedDevice?.serial !== serial) throw new Error("请先重新检测设备");
  const microphone = browserSnapshot.hardwareTests.find(test => test.id === "microphone");
  if (microphone?.status === "running") throw new Error("麦克风正在采集，请等待本次录音完成");
  if (!microphone || microphone.status === "waiting") throw new Error("请先完成自动硬件检测");
  const startingTests = browserSnapshot.hardwareTests.map(test => test.id === "microphone" ? {
    ...test,
    status: "running" as const,
    summary: "正在录制 3 秒，请靠近设备正常说话",
    detail: "与涂鸦共用 default/CaptureDsnoop 真实采集链路",
    value: undefined,
    rawOutput: undefined,
    durationMs: undefined
  } : test);
  const startingResolved = startingTests.filter(test => test.status === "passed" || test.status === "failed").length;
  browserSnapshot = {
    ...browserSnapshot,
    hardwareTests: startingTests,
    hardwareProgress: Math.round((startingResolved / startingTests.length) * 100),
    progress: Math.round((startingResolved / startingTests.length) * 100),
    hardwareStatus: "running",
    statusMessage: "麦克风正在重新采集，请持续说话 3 秒",
    events: [{ timestamp: now(), level: "info", message: "麦克风开始采集，请持续说话 3 秒" }, ...browserSnapshot.events]
  };
  publishBrowser();
  try {
    const result = await localApi<HardwareTestResult>("microphone", { serial });
    const hardwareTests = browserSnapshot.hardwareTests.map(test => test.id === "microphone" ? result : test);
    const resolved = hardwareTests.filter(test => test.status === "passed" || test.status === "failed").length;
    const anyFailed = hardwareTests.some(test => test.status === "failed");
    browserSnapshot = {
      ...browserSnapshot,
      hardwareTests,
      hardwareProgress: Math.round((resolved / hardwareTests.length) * 100),
      progress: Math.round((resolved / hardwareTests.length) * 100),
      hardwareStatus: anyFailed ? "failed" : "needsConfirmation",
      statusMessage: result.status === "failed" ? "麦克风输入电平异常" : "麦克风已回放，等待人工确认音质",
      events: [{
        timestamp: now(),
        level: result.status === "failed" ? "error" : "success",
        message: `麦克风采集完成：${result.value || result.summary}`
      }, ...browserSnapshot.events]
    };
    publishBrowser();
    return clone();
  } catch (reason) {
    const message = reason instanceof Error ? reason.message : String(reason);
    browserSnapshot = {
      ...browserSnapshot,
      hardwareStatus: "failed",
      statusMessage: "麦克风检测失败",
      hardwareTests: browserSnapshot.hardwareTests.map(test => test.id === "microphone" ? {
        ...test,
        status: "failed",
        summary: "真实麦克风录制失败",
        detail: message,
        rawOutput: `[stderr]\n${message}`
      } : test),
      events: [{ timestamp: now(), level: "error", message }, ...browserSnapshot.events]
    };
    publishBrowser();
    throw reason;
  }
}

export async function resetRun(): Promise<StationSnapshot> {
  if (isTauri()) return invoke("reset_run");
  browserSnapshot = {
    ...browserSnapshot,
    busy: false,
    phase: "idle",
    progress: 0,
    statusMessage: browserSnapshot.selectedDevice ? "设备身份已读取" : "等待设备接入",
    hardwareStatus: "idle",
    hardwareProgress: 0,
    hardwareTests: browserSnapshot.hardwareTests.map(test => ({
      ...test,
      status: "waiting",
      value: undefined,
      detail: undefined,
      rawOutput: undefined,
      durationMs: undefined
    }))
  };
  publishBrowser();
  return clone();
}

export async function onSnapshot(
  handler: (snapshot: StationSnapshot) => void
): Promise<UnlistenFn> {
  if (!isTauri()) {
    browserListeners.add(handler);
    return () => browserListeners.delete(handler);
  }
  return listen<StationSnapshot>("factory://snapshot", event => handler(event.payload));
}
