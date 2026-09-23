use crate::{
    adb,
    model::{DeviceIdentity, HardwareTestKind, HardwareTestResult, HardwareTestStatus},
};
use std::{fs, time::Instant};

pub const AUTOMATIC_TEST_IDS: &[&str] = &[
    "identity",
    "storage",
    "memory",
    "wifi",
    "bluetooth",
    "environment",
    "power",
    "audio",
    "touch-controller",
];

pub const INTERACTIVE_TEST_IDS: &[&str] = &["screen", "touch", "speaker", "microphone"];

const MICROPHONE_COMMAND: &str =
    "arecord -D default -t wav -f S16_LE -r 16000 -c 1 -d 3 /tmp/aitvbox_factory_mic.wav";

pub fn automatic_command(id: &str) -> Option<&'static str> {
    match id {
        "identity" => Some("sed -n 's/^[[:space:]]*sunxi_serial[[:space:]]*:[[:space:]]*//p' /sys/class/sunxi_info/sys_info | head -1; sed -n 's/^AITVBOX_VERSION=//p' /etc/aitvbox-version | head -1"),
        "storage" => Some("cat /sys/class/block/mmcblk0/size"),
        "memory" => Some("awk '/^MemTotal:/ {print $2}' /proc/meminfo"),
        "wifi" => Some("mac=$(cat /sys/class/net/wlan0/address 2>/dev/null) || exit 1; state=$(cat /sys/class/net/wlan0/operstate 2>/dev/null); scan=$(iw dev wlan0 scan 2>/dev/null | awk '/^BSS /{n++} /signal:/{if(!seen || $2>best){best=$2;seen=1}} END{if(n>0) printf \"%d %.0f\",n,best}'); printf '%s\\n%s\\n%s\\n' \"$mac\" \"$state\" \"$scan\""),
        "bluetooth" => Some("hciconfig hci0 2>/dev/null"),
        "environment" => Some("for d in /sys/class/hwmon/hwmon*; do n=$(cat \"$d/name\" 2>/dev/null); case \"$n\" in *aht20*) cat \"$d/temp1_input\" \"$d/humidity1_input\" 2>/dev/null; exit;; esac; done; exit 1"),
        "power" => Some("for d in /sys/class/hwmon/hwmon*; do n=$(cat \"$d/name\" 2>/dev/null); case \"$n\" in *ina219*) cat \"$d/in1_input\" \"$d/curr1_input\" \"$d/power1_input\" 2>/dev/null; exit;; esac; done; exit 1"),
        "audio" => Some("cat /proc/asound/cards 2>/dev/null"),
        "touch-controller" => Some("awk '/^N: Name=/{n=$0} /Handlers=.*event[0-9]+/{if(n ~ /gt9xxnew_ts/){print n; print $0; exit}}' /proc/bus/input/devices"),
        _ => None,
    }
}

fn waiting(
    id: &str,
    label: &str,
    group: &str,
    kind: HardwareTestKind,
    summary: &str,
) -> HardwareTestResult {
    HardwareTestResult {
        id: id.to_string(),
        label: label.to_string(),
        group: group.to_string(),
        kind,
        status: HardwareTestStatus::Waiting,
        summary: summary.to_string(),
        value: None,
        detail: None,
        command: automatic_command(id).map(str::to_string),
        raw_output: None,
        duration_ms: None,
    }
}

pub fn initial_results() -> Vec<HardwareTestResult> {
    vec![
        waiting(
            "identity",
            "设备身份",
            "基础",
            HardwareTestKind::Automatic,
            "校验 CPUID 与固件版本",
        ),
        waiting(
            "storage",
            "存储容量",
            "基础",
            HardwareTestKind::Automatic,
            "读取 eMMC 实际容量",
        ),
        waiting(
            "memory",
            "运行内存",
            "基础",
            HardwareTestKind::Automatic,
            "读取系统可用内存规格",
        ),
        waiting(
            "wifi",
            "Wi-Fi 模组",
            "无线",
            HardwareTestKind::Automatic,
            "校验 wlan0、硬件地址与 RF 扫描",
        ),
        waiting(
            "bluetooth",
            "蓝牙控制器",
            "无线",
            HardwareTestKind::Automatic,
            "校验 hci0 工作状态",
        ),
        waiting(
            "environment",
            "温湿度传感器",
            "传感",
            HardwareTestKind::Automatic,
            "读取 AHT20 实测数据",
        ),
        waiting(
            "power",
            "电源监测",
            "传感",
            HardwareTestKind::Automatic,
            "读取 INA219 电压电流",
        ),
        waiting(
            "audio",
            "音频设备",
            "多媒体",
            HardwareTestKind::Automatic,
            "校验 ALSA 播放设备",
        ),
        waiting(
            "touch-controller",
            "触控控制器",
            "交互",
            HardwareTestKind::Automatic,
            "识别 gt9xx 输入节点",
        ),
        waiting(
            "screen",
            "屏幕显示",
            "人工确认",
            HardwareTestKind::Interactive,
            "检查亮点、暗点与色彩",
        ),
        waiting(
            "touch",
            "触摸响应",
            "人工确认",
            HardwareTestKind::Interactive,
            "确认全屏触控连续有效",
        ),
        waiting(
            "speaker",
            "扬声器",
            "人工确认",
            HardwareTestKind::Interactive,
            "确认测试音清晰无杂音",
        ),
        HardwareTestResult {
            command: Some(MICROPHONE_COMMAND.to_string()),
            ..waiting(
                "microphone",
                "麦克风",
                "人工确认",
                HardwareTestKind::Interactive,
                "说话 3 秒，检测真实输入电平并原音回放",
            )
        },
    ]
}

fn result(
    template: &HardwareTestResult,
    passed: bool,
    summary: impl Into<String>,
    value: Option<String>,
    detail: Option<String>,
    raw_output: Option<String>,
    duration_ms: u64,
) -> HardwareTestResult {
    HardwareTestResult {
        status: if passed {
            HardwareTestStatus::Passed
        } else {
            HardwareTestStatus::Failed
        },
        summary: summary.into(),
        value,
        detail,
        raw_output,
        duration_ms: Some(duration_ms),
        ..template.clone()
    }
}

fn measured<F>(template: &HardwareTestResult, test: F) -> HardwareTestResult
where
    F: FnOnce() -> (bool, String, Option<String>, Option<String>, Option<String>),
{
    let started = Instant::now();
    let (passed, summary, value, detail, raw_output) = test();
    result(
        template,
        passed,
        summary,
        value,
        detail,
        raw_output,
        started.elapsed().as_millis() as u64,
    )
}

fn command_result(serial: &str, command: &str) -> Result<String, String> {
    adb::shell(serial, command).map(|output| output.trim().to_string())
}

fn evidence_output(raw: &str) -> String {
    const MAX_CHARS: usize = 4_000;
    let mut output: String = raw.chars().take(MAX_CHARS).collect();
    if raw.chars().count() > MAX_CHARS {
        output.push_str("\n… 输出已截断（仅保留前 4000 个字符）");
    }
    output
}

fn error_output(error: &str) -> String {
    format!("[stderr]\n{error}")
}

fn parse_u64(value: &str) -> Option<u64> {
    value.trim().lines().next()?.trim().parse().ok()
}

fn expected_storage_gb() -> f64 {
    std::env::var("HW_EXPECTED_STORAGE_GB")
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(16.0)
}

fn expected_memory_mib() -> f64 {
    std::env::var("HW_EXPECTED_MEMORY_MIB")
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(2048.0)
}

fn storage_matches_bom(bytes: f64, expected_gb: f64) -> bool {
    bytes >= expected_gb * 1_000_000_000.0 * 0.95
}

fn memory_matches_bom(visible_mib: f64, expected_mib: f64) -> bool {
    visible_mib >= expected_mib * 0.90
}

pub fn run_automatic(
    serial: &str,
    id: &str,
    _identity: Option<&DeviceIdentity>,
) -> HardwareTestResult {
    let template = initial_results()
        .into_iter()
        .find(|item| item.id == id)
        .expect("automatic test id must have a template");
    let command = template.command.clone().unwrap_or_default();

    measured(&template, || match command_result(serial, &command) {
        Ok(raw) => {
            let evidence = Some(evidence_output(&raw));
            match id {
                "identity" => {
                    let mut lines = raw.lines();
                    let cpuid = lines.next().unwrap_or_default().trim().to_ascii_lowercase();
                    let firmware = lines.next().unwrap_or_default().trim().to_string();
                    let cpuid_ok = cpuid.len() == 32
                        && cpuid.chars().all(|character| character.is_ascii_hexdigit());
                    let passed = cpuid_ok && !firmware.is_empty();
                    (
                        passed,
                        if passed {
                            "身份信息完整"
                        } else {
                            "身份信息不完整"
                        }
                        .to_string(),
                        (!firmware.is_empty()).then(|| format!("固件 {firmware}")),
                        (!passed)
                            .then(|| "需要同时读到 32 位十六进制 CPUID 和固件版本".to_string()),
                        evidence,
                    )
                }
                "storage" => match parse_u64(&raw) {
                    Some(sectors) => {
                        let bytes = sectors as f64 * 512.0;
                        let gib = bytes / 1024.0 / 1024.0 / 1024.0;
                        let expected_gb = expected_storage_gb();
                        let passed = storage_matches_bom(bytes, expected_gb);
                        (
                            passed,
                            if passed {
                                "eMMC 容量符合 BOM"
                            } else {
                                "eMMC 容量规格不符"
                            }
                            .to_string(),
                            Some(format!("{expected_gb:.0} GB 标称 · {gib:.1} GiB 实测")),
                            (!passed)
                                .then(|| format!("当前容量低于 {expected_gb:.0} GB 规格允许范围")),
                            evidence,
                        )
                    }
                    None => (
                        false,
                        "无法读取 eMMC".to_string(),
                        None,
                        Some("容量数据格式无效".to_string()),
                        evidence,
                    ),
                },
                "memory" => match parse_u64(&raw) {
                    Some(kib) => {
                        let mib = kib as f64 / 1024.0;
                        let expected_mib = expected_memory_mib();
                        let passed = memory_matches_bom(mib, expected_mib);
                        (
                            passed,
                            if passed {
                                "运行内存符合 BOM"
                            } else {
                                "运行内存规格不符"
                            }
                            .to_string(),
                            Some(format!(
                                "{:.0} GB 标称 · {mib:.0} MiB 系统可见",
                                expected_mib / 1024.0
                            )),
                            (!passed).then(|| {
                                format!("当前内存低于 {:.0} GB 规格允许范围", expected_mib / 1024.0)
                            }),
                            evidence,
                        )
                    }
                    None => (
                        false,
                        "无法读取运行内存".to_string(),
                        None,
                        Some("内存数据格式无效".to_string()),
                        evidence,
                    ),
                },
                "wifi" => {
                    let mut lines = raw.lines();
                    let mac = lines.next().unwrap_or_default().trim().to_ascii_uppercase();
                    let state = lines.next().unwrap_or("unknown").trim();
                    let mut scan = lines.next().unwrap_or_default().split_whitespace();
                    let ap_count = scan
                        .next()
                        .and_then(|value| value.parse::<u32>().ok())
                        .unwrap_or(0);
                    let best_signal = scan.next().and_then(|value| value.parse::<i32>().ok());
                    let valid_mac = mac.len() == 17 && mac != "00:00:00:00:00:00";
                    let passed = valid_mac && ap_count > 0;
                    (
                        passed,
                        if passed {
                            "无线控制器与 RF 扫描正常"
                        } else {
                            "无线射频检测未通过"
                        }
                        .to_string(),
                        passed.then(|| {
                            format!(
                                "{ap_count} 个 AP · {} dBm · {state}",
                                best_signal.unwrap_or(0)
                            )
                        }),
                        (!passed).then(|| {
                            "wlan0/MAC 无效，或附近没有扫描到无线接入点；不要求设备连接网络"
                                .to_string()
                        }),
                        evidence,
                    )
                }
                "bluetooth" => {
                    let up = raw.contains("UP RUNNING")
                        || raw.lines().any(|line| line.trim_start().starts_with("UP "));
                    let address = raw
                        .split_whitespace()
                        .skip_while(|part| *part != "Address:")
                        .nth(1)
                        .map(str::to_string);
                    (
                        up,
                        if up {
                            "控制器运行正常"
                        } else {
                            "控制器未进入 UP 状态"
                        }
                        .to_string(),
                        address,
                        (!up).then(|| "hci0 已出现但没有进入 UP/RUNNING".to_string()),
                        evidence,
                    )
                }
                "environment" => {
                    let values: Vec<f64> = raw
                        .lines()
                        .filter_map(|line| line.trim().parse::<f64>().ok())
                        .collect();
                    if values.len() >= 2 {
                        let temp = values[0] / 1000.0;
                        let humidity = values[1] / 1000.0;
                        let sane =
                            (-20.0..=80.0).contains(&temp) && (0.0..=100.0).contains(&humidity);
                        (
                            sane,
                            "AHT20 返回实测数据".to_string(),
                            Some(format!("{temp:.1} °C · {humidity:.1} %RH")),
                            (!sane).then(|| "读数超出传感器合理范围".to_string()),
                            evidence,
                        )
                    } else {
                        (
                            false,
                            "AHT20 数据不完整".to_string(),
                            None,
                            Some("没有同时读到温度和湿度".to_string()),
                            evidence,
                        )
                    }
                }
                "power" => {
                    let values: Vec<f64> = raw
                        .lines()
                        .filter_map(|line| line.trim().parse::<f64>().ok())
                        .collect();
                    if values.len() >= 3 {
                        let millivolts = values[0];
                        let milliamps = values[1];
                        let milliwatts = values[2] / 1000.0;
                        let sane = millivolts > 0.0 && milliamps >= 0.0;
                        (
                            sane,
                            "INA219 返回实测数据".to_string(),
                            Some(format!(
                                "{:.2} V · {:.0} mA · {:.2} W",
                                millivolts / 1000.0,
                                milliamps,
                                milliwatts / 1000.0
                            )),
                            (!sane).then(|| "电压或电流读数无效".to_string()),
                            evidence,
                        )
                    } else {
                        (
                            false,
                            "INA219 数据不完整".to_string(),
                            None,
                            Some("没有同时读到电压、电流和功率".to_string()),
                            evidence,
                        )
                    }
                }
                "audio" => {
                    let found = !raw.trim().is_empty() && !raw.contains("no soundcards");
                    let card = raw
                        .lines()
                        .find(|line| line.contains('['))
                        .map(str::trim)
                        .unwrap_or("ALSA card");
                    (
                        found,
                        "ALSA 播放设备已识别".to_string(),
                        found.then(|| card.chars().take(52).collect()),
                        (!found).then(|| "没有发现 ALSA 声卡；扬声器音质仍需人工确认".to_string()),
                        evidence,
                    )
                }
                "touch-controller" => {
                    let found = raw.contains("gt9xxnew_ts") && raw.contains("event");
                    let node = raw
                        .split_whitespace()
                        .find(|part| part.starts_with("event"))
                        .map(|part| format!("/dev/input/{}", part.trim_end_matches('"')));
                    (
                        found,
                        "触控驱动已识别".to_string(),
                        node,
                        (!found).then(|| "没有找到 gt9xxnew_ts 的 event 输入节点".to_string()),
                        evidence,
                    )
                }
                _ => (
                    false,
                    "未知检测项目".to_string(),
                    None,
                    Some(id.to_string()),
                    evidence,
                ),
            }
        }
        Err(error) => (
            false,
            match id {
                "storage" => "无法读取 eMMC",
                "memory" => "无法读取运行内存",
                "wifi" => "无线网卡未识别",
                "bluetooth" => "未找到蓝牙控制器",
                "environment" => "未检测到 AHT20",
                "power" => "未检测到 INA219",
                "audio" => "未找到音频设备",
                "touch-controller" => "触控驱动未识别",
                "identity" => "身份信息读取失败",
                _ => "检测命令执行失败",
            }
            .to_string(),
            None,
            Some(error.clone()),
            Some(error_output(&error)),
        ),
    })
}

pub fn play_test_tone(serial: &str) -> Result<(), String> {
    const SAMPLE_RATE: u32 = 16_000;
    const DURATION_MS: u32 = 1_200;
    const CHANNELS: u16 = 1;
    const BITS_PER_SAMPLE: u16 = 16;
    let sample_count = SAMPLE_RATE * DURATION_MS / 1000;
    let data_size = sample_count * u32::from(CHANNELS) * u32::from(BITS_PER_SAMPLE / 8);
    let mut wav = Vec::with_capacity(44 + data_size as usize);
    wav.extend_from_slice(b"RIFF");
    wav.extend_from_slice(&(36 + data_size).to_le_bytes());
    wav.extend_from_slice(b"WAVEfmt ");
    wav.extend_from_slice(&16u32.to_le_bytes());
    wav.extend_from_slice(&1u16.to_le_bytes());
    wav.extend_from_slice(&CHANNELS.to_le_bytes());
    wav.extend_from_slice(&SAMPLE_RATE.to_le_bytes());
    wav.extend_from_slice(&(SAMPLE_RATE * u32::from(CHANNELS) * 2).to_le_bytes());
    wav.extend_from_slice(&(CHANNELS * 2).to_le_bytes());
    wav.extend_from_slice(&BITS_PER_SAMPLE.to_le_bytes());
    wav.extend_from_slice(b"data");
    wav.extend_from_slice(&data_size.to_le_bytes());

    for index in 0..sample_count {
        let elapsed = index as f32 / SAMPLE_RATE as f32;
        let frequency = if index < sample_count / 2 {
            660.0
        } else {
            880.0
        };
        let envelope = ((index.min(sample_count - index)) as f32 / 480.0).min(1.0);
        let sample = ((elapsed * frequency * std::f32::consts::TAU).sin()
            * 0.28
            * envelope
            * i16::MAX as f32) as i16;
        wav.extend_from_slice(&sample.to_le_bytes());
    }

    let local =
        std::env::temp_dir().join(format!("aitvbox-factory-tone-{}.wav", std::process::id()));
    let remote = "/tmp/aitvbox_factory_tone.wav";
    fs::write(&local, wav).map_err(|error| format!("无法生成测试音: {error}"))?;
    let push_result = adb::push_file(serial, &local, remote);
    let _ = fs::remove_file(&local);
    push_result?;
    adb::shell(
        serial,
        "aplay /tmp/aitvbox_factory_tone.wav >/dev/null 2>&1; code=$?; rm -f /tmp/aitvbox_factory_tone.wav; exit $code",
    )
    .map(|_| ())
    .map_err(|error| format!("测试音播放失败: {error}"))
}

fn wav_pcm16_mono_metrics(bytes: &[u8]) -> Result<(u32, usize, f64, f64), String> {
    if bytes.len() < 44 || &bytes[0..4] != b"RIFF" || &bytes[8..12] != b"WAVE" {
        return Err("设备返回的录音不是有效 WAV 文件".to_string());
    }

    let mut offset = 12usize;
    let mut format = None;
    let mut pcm = None;
    while offset + 8 <= bytes.len() {
        let chunk_id = &bytes[offset..offset + 4];
        let chunk_size = u32::from_le_bytes(
            bytes[offset + 4..offset + 8]
                .try_into()
                .map_err(|_| "WAV 块长度无效".to_string())?,
        ) as usize;
        let start = offset + 8;
        let end = start.saturating_add(chunk_size).min(bytes.len());
        if chunk_id == b"fmt " && end >= start + 16 {
            format = Some((
                u16::from_le_bytes([bytes[start], bytes[start + 1]]),
                u16::from_le_bytes([bytes[start + 2], bytes[start + 3]]),
                u32::from_le_bytes(bytes[start + 4..start + 8].try_into().unwrap()),
                u16::from_le_bytes([bytes[start + 14], bytes[start + 15]]),
            ));
        } else if chunk_id == b"data" {
            pcm = Some(&bytes[start..end]);
        }
        offset = start.saturating_add(chunk_size + (chunk_size & 1));
    }

    let (encoding, channels, sample_rate, bits) =
        format.ok_or_else(|| "WAV 缺少 fmt 参数".to_string())?;
    if encoding != 1 || channels != 1 || sample_rate != 16_000 || bits != 16 {
        return Err(format!(
            "录音格式不符合产品链路：encoding={encoding}, channels={channels}, rate={sample_rate}, bits={bits}"
        ));
    }
    let pcm = pcm.ok_or_else(|| "WAV 缺少 PCM 数据".to_string())?;
    let samples: Vec<f64> = pcm
        .chunks_exact(2)
        .map(|chunk| i16::from_le_bytes([chunk[0], chunk[1]]) as f64)
        .collect();
    if samples.len() < 16_000 {
        return Err(format!(
            "有效录音不足 1 秒，仅有 {} 个采样点",
            samples.len()
        ));
    }

    // 去掉直流偏置后再统计，避免硬件偏置被误判成有效人声。
    let mean = samples.iter().sum::<f64>() / samples.len() as f64;
    let mut square_sum = 0.0;
    let mut peak = 0.0f64;
    for sample in &samples {
        let centered = sample - mean;
        square_sum += centered * centered;
        peak = peak.max(centered.abs());
    }
    let rms = (square_sum / samples.len() as f64).sqrt() / 32768.0;
    let peak = peak / 32768.0;
    let to_dbfs = |amplitude: f64| {
        if amplitude <= 0.000_001 {
            -120.0
        } else {
            20.0 * amplitude.log10()
        }
    };
    Ok((sample_rate, samples.len(), to_dbfs(rms), to_dbfs(peak)))
}

pub fn run_microphone_test(serial: &str) -> Result<HardwareTestResult, String> {
    let started = Instant::now();
    let mut template = initial_results()
        .into_iter()
        .find(|test| test.id == "microphone")
        .ok_or_else(|| "麦克风检测模板缺失".to_string())?;
    let remote = "/tmp/aitvbox_factory_mic.wav";
    let local =
        std::env::temp_dir().join(format!("aitvbox-factory-mic-{}.wav", std::process::id()));

    // The production ALSA default capture is CaptureDsnoop, shared by Tuya and
    // the factory assistant. This measures the exact product input without
    // stopping lv_backend, Tuya, Bluetooth, or the device UI.
    let capture_command = concat!(
        "grep -q 'capture.pcm \"CaptureDsnoop\"' /etc/asound.conf || { ",
        "echo '当前固件未启用共享麦克风采集，请烧录包含 CaptureDsnoop 的量产固件' >&2; exit 42; }; ",
        "rm -f /tmp/aitvbox_factory_mic.wav; ",
        "arecord -D default -t wav -f S16_LE -r 16000 -c 1 -d 3 ",
        "/tmp/aitvbox_factory_mic.wav 2>&1"
    );
    let capture_output = adb::shell(serial, capture_command)
        .map_err(|error| format!("真实麦克风录制失败: {error}"))?;
    if let Err(error) = adb::pull_file(serial, remote, &local) {
        let _ = adb::shell(serial, "rm -f /tmp/aitvbox_factory_mic.wav");
        return Err(format!("无法读取麦克风录音: {error}"));
    }

    let bytes = fs::read(&local).map_err(|error| format!("无法分析麦克风录音: {error}"));
    let _ = fs::remove_file(&local);
    let bytes = bytes?;
    let metrics = wav_pcm16_mono_metrics(&bytes);

    let playback_result = adb::shell(
        serial,
        "aplay /tmp/aitvbox_factory_mic.wav >/dev/null 2>&1; code=$?; rm -f /tmp/aitvbox_factory_mic.wav; exit $code",
    );
    playback_result.map_err(|error| format!("麦克风原音回放失败: {error}"))?;

    let (sample_rate, sample_count, rms_dbfs, peak_dbfs) = metrics?;
    let min_rms = std::env::var("HW_MIC_MIN_RMS_DBFS")
        .ok()
        .and_then(|value| value.parse::<f64>().ok())
        .unwrap_or(-50.0);
    let min_peak = std::env::var("HW_MIC_MIN_PEAK_DBFS")
        .ok()
        .and_then(|value| value.parse::<f64>().ok())
        .unwrap_or(-35.0);
    let has_signal = rms_dbfs >= min_rms && peak_dbfs >= min_peak;
    let duration_ms = started.elapsed().as_millis() as u64;
    template.status = if has_signal {
        HardwareTestStatus::NeedsConfirmation
    } else {
        HardwareTestStatus::Failed
    };
    template.summary = if has_signal {
        "已检测到有效输入并完成原音回放"
    } else {
        "录音电平过低，请靠近设备说话后重试"
    }
    .to_string();
    template.value = Some(format!("RMS {rms_dbfs:.1} dBFS · 峰值 {peak_dbfs:.1} dBFS"));
    template.detail = Some(
        if has_signal {
            "请确认刚才回放的人声清晰、无明显底噪、削波或破音"
        } else {
            "没有达到有效人声阈值，不能仅凭 ALSA 设备存在判定麦克风通过"
        }
        .to_string(),
    );
    template.raw_output = Some(format!(
        "采集端口: default → CaptureDsnoop → hw:audiocodec,0\n格式: PCM S16_LE / {sample_rate} Hz / 单声道\n采样点: {sample_count}\n文件大小: {} bytes\nRMS: {rms_dbfs:.2} dBFS (阈值 {min_rms:.1})\nPeak: {peak_dbfs:.2} dBFS (阈值 {min_peak:.1})\narecord: {}",
        bytes.len(),
        if capture_output.is_empty() { "完成" } else { capture_output.as_str() }
    ));
    template.duration_ms = Some(duration_ms);
    Ok(template)
}

#[cfg(test)]
mod tests {
    use super::{
        initial_results, memory_matches_bom, storage_matches_bom, wav_pcm16_mono_metrics,
        AUTOMATIC_TEST_IDS, INTERACTIVE_TEST_IDS,
    };

    fn pcm16_wav(samples: &[i16]) -> Vec<u8> {
        let data_size = (samples.len() * 2) as u32;
        let mut wav = Vec::with_capacity(44 + data_size as usize);
        wav.extend_from_slice(b"RIFF");
        wav.extend_from_slice(&(36 + data_size).to_le_bytes());
        wav.extend_from_slice(b"WAVEfmt ");
        wav.extend_from_slice(&16u32.to_le_bytes());
        wav.extend_from_slice(&1u16.to_le_bytes());
        wav.extend_from_slice(&1u16.to_le_bytes());
        wav.extend_from_slice(&16_000u32.to_le_bytes());
        wav.extend_from_slice(&32_000u32.to_le_bytes());
        wav.extend_from_slice(&2u16.to_le_bytes());
        wav.extend_from_slice(&16u16.to_le_bytes());
        wav.extend_from_slice(b"data");
        wav.extend_from_slice(&data_size.to_le_bytes());
        for sample in samples {
            wav.extend_from_slice(&sample.to_le_bytes());
        }
        wav
    }

    #[test]
    fn every_test_id_is_unique_and_accounted_for() {
        let results = initial_results();
        let mut ids: Vec<_> = results.iter().map(|item| item.id.as_str()).collect();
        ids.sort_unstable();
        ids.dedup();
        assert_eq!(ids.len(), results.len());
        assert_eq!(
            results.len(),
            AUTOMATIC_TEST_IDS.len() + INTERACTIVE_TEST_IDS.len()
        );
    }

    #[test]
    fn bom_capacity_checks_reject_wrong_memory_and_storage_sku() {
        assert!(storage_matches_bom(15_634_268_160.0, 16.0));
        assert!(!storage_matches_bom(7_817_134_080.0, 16.0));
        assert!(memory_matches_bom(1985.6, 2048.0));
        assert!(!memory_matches_bom(923.0, 2048.0));
    }

    #[test]
    fn microphone_metrics_read_real_pcm_instead_of_wav_headers() {
        let samples: Vec<i16> = (0..16_000)
            .map(|index| if index % 2 == 0 { 1_000 } else { -1_000 })
            .collect();
        let (rate, count, rms, peak) = wav_pcm16_mono_metrics(&pcm16_wav(&samples)).unwrap();
        assert_eq!(rate, 16_000);
        assert_eq!(count, 16_000);
        assert!((-31.0..-29.0).contains(&rms));
        assert!((-31.0..-29.0).contains(&peak));
    }
}
