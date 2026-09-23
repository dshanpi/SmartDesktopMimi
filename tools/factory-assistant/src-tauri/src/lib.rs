use aitvbox_factory_core::license_store::{ImportReport, LicenseStore};
use aitvbox_factory_core::model::{
    DeviceIdentity, EventLevel, EventRecord, HardwareStatus, HardwareTestKind,
    HardwareTestStatus, LicenseInventoryView, RunPhase, StationSnapshot,
};
use aitvbox_factory_core::pipeline::{self, EventLevelLite};
use aitvbox_factory_core::station::StationConfig;
use aitvbox_factory_core::{adb, hardware};
use chrono::Local;
use std::{
    env, path::PathBuf, sync::Mutex, thread,
};
use tauri::{AppHandle, Emitter, Manager, State};

struct FactoryState {
    snapshot: Mutex<StationSnapshot>,
}

#[cfg(target_os = "windows")]
fn configure_bundled_adb(app: &tauri::App) -> Result<(), Box<dyn std::error::Error>> {
    if env::var_os("AITVBOX_ADB_PATH").is_some() {
        return Ok(());
    }
    let adb = app.path().resource_dir()?.join("platform-tools").join("adb.exe");
    if adb.is_file() {
        env::set_var("AITVBOX_ADB_PATH", adb);
    }
    Ok(())
}

#[cfg(not(target_os = "windows"))]
fn configure_bundled_adb(_app: &tauri::App) -> Result<(), Box<dyn std::error::Error>> {
    Ok(())
}

fn timestamp() -> String {
    Local::now().format("%H:%M:%S").to_string()
}

fn event(level: EventLevel, message: impl Into<String>) -> EventRecord {
    EventRecord {
        timestamp: timestamp(),
        level,
        message: message.into(),
    }
}

fn inventory_view(config: &StationConfig) -> Option<LicenseInventoryView> {
    let store = LicenseStore::open(&config.data_dir).ok()?;
    let s = store.summary(&config.tuya_pid).ok()?;
    Some(LicenseInventoryView {
        available: s.available,
        reserved: s.reserved,
        written: s.written,
        shipped: s.shipped,
        total: s.total,
    })
}

fn initial_snapshot() -> StationSnapshot {
    let config = StationConfig::load();
    let checks = config.config_checks();
    let station_configured = config.station_ready();
    StationSnapshot {
        host_platform: format!("{} {}", env::consts::OS, env::consts::ARCH),
        station_name: config.station_id.clone(),
        batch_number: config.batch_number.clone(),
        adb_available: adb::available(),
        cloud_configured: station_configured,
        busy: false,
        phase: RunPhase::Idle,
        progress: 0,
        status_message: "等待设备接入".to_string(),
        devices: Vec::new(),
        selected_device: None,
        config_checks: checks,
        hardware_status: HardwareStatus::Idle,
        hardware_progress: 0,
        hardware_tests: hardware::initial_results(),
        events: vec![event(
            EventLevel::Info,
            "出厂助手已启动（本地签名 + 涂鸦库存，不依赖量产云 API）",
        )],
        license_inventory: inventory_view(&config),
        tuya_pid: config.tuya_pid.clone(),
        factory_home: config.factory_home.display().to_string(),
        license_db_path: config.license_db_path().display().to_string(),
    }
}

fn push_event(snapshot: &mut StationSnapshot, level: EventLevel, message: impl Into<String>) {
    snapshot.events.insert(0, event(level, message));
    snapshot.events.truncate(40);
}

fn publish(app: &AppHandle, snapshot: &StationSnapshot) {
    let _ = app.emit("factory://snapshot", snapshot.clone());
}

fn mutate<F>(app: &AppHandle, action: F) -> Result<StationSnapshot, String>
where
    F: FnOnce(&mut StationSnapshot),
{
    let state = app.state::<FactoryState>();
    let mut snapshot = state
        .snapshot
        .lock()
        .map_err(|_| "工位状态锁异常".to_string())?;
    action(&mut snapshot);
    let result = snapshot.clone();
    drop(snapshot);
    publish(app, &result);
    Ok(result)
}

#[tauri::command]
fn get_station_snapshot(state: State<'_, FactoryState>) -> Result<StationSnapshot, String> {
    state
        .snapshot
        .lock()
        .map(|snapshot| snapshot.clone())
        .map_err(|_| "工位状态锁异常".to_string())
}

#[tauri::command]
fn scan_devices(app: AppHandle) -> Result<StationSnapshot, String> {
    mutate(&app, |snapshot| {
        snapshot.phase = RunPhase::Scanning;
        snapshot.status_message = "正在扫描 ADB 设备".to_string();
    })?;

    let devices = match adb::scan() {
        Ok(devices) => devices,
        Err(error) => {
            mutate(&app, |snapshot| {
                snapshot.phase = RunPhase::Failed;
                snapshot.status_message = "ADB 扫描失败".to_string();
                push_event(snapshot, EventLevel::Error, error.clone());
            })?;
            return Err(error);
        }
    };

    let usable: Vec<_> = devices
        .iter()
        .filter(|device| device.state == "device")
        .collect();
    let usable_count = usable.len();
    let (identity, inspect_error) = if usable_count == 1 {
        match adb::inspect(&usable[0].serial) {
            Ok(identity) if identity.cpuid.is_some() => (Some(identity), None),
            Ok(_) => (None, Some("设备没有返回有效 CPUID".to_string())),
            Err(error) => (None, Some(error)),
        }
    } else {
        (None, None)
    };
    drop(usable);

    let config = StationConfig::load();
    mutate(&app, |snapshot| {
        snapshot.devices = devices;
        snapshot.selected_device = identity;
        snapshot.phase = RunPhase::Idle;
        snapshot.progress = 0;
        snapshot.hardware_status = HardwareStatus::Idle;
        snapshot.hardware_progress = 0;
        snapshot.hardware_tests = hardware::initial_results();
        snapshot.cloud_configured = config.station_ready();
        snapshot.config_checks = config.config_checks();
        snapshot.license_inventory = inventory_view(&config);
        snapshot.tuya_pid = config.tuya_pid.clone();
        snapshot.station_name = config.station_id.clone();
        snapshot.batch_number = config.batch_number.clone();
        snapshot.factory_home = config.factory_home.display().to_string();
        snapshot.license_db_path = config.license_db_path().display().to_string();
        match usable_count {
            0 => {
                snapshot.status_message = "等待设备接入".to_string();
                push_event(snapshot, EventLevel::Warning, "没有发现可用的 ADB 设备");
            }
            1 => {
                if let Some(error) = inspect_error.clone() {
                    snapshot.phase = RunPhase::Failed;
                    snapshot.status_message = "设备身份读取失败".to_string();
                    push_event(snapshot, EventLevel::Error, error);
                } else {
                    snapshot.status_message = "设备身份已读取".to_string();
                    push_event(snapshot, EventLevel::Success, "发现 1 台可生产设备");
                }
            }
            count => {
                snapshot.status_message = "检测到多台设备，请只保留当前工位设备".to_string();
                push_event(
                    snapshot,
                    EventLevel::Error,
                    format!("检测到 {count} 台 ADB 设备，已禁止开始生产"),
                );
            }
        }
    })
}

fn hardware_progress(snapshot: &StationSnapshot) -> u8 {
    let resolved = snapshot
        .hardware_tests
        .iter()
        .filter(|test| {
            matches!(
                test.status,
                HardwareTestStatus::Passed | HardwareTestStatus::Failed
            )
        })
        .count();
    if snapshot.hardware_tests.is_empty() {
        0
    } else {
        ((resolved * 100) / snapshot.hardware_tests.len()) as u8
    }
}

#[tauri::command]
fn start_hardware_tests(app: AppHandle, serial: String) -> Result<StationSnapshot, String> {
    {
        let state = app.state::<FactoryState>();
        let snapshot = state
            .snapshot
            .lock()
            .map_err(|_| "工位状态锁异常".to_string())?;
        if snapshot.busy {
            return Err("当前检测仍在运行".to_string());
        }
        if snapshot
            .selected_device
            .as_ref()
            .map(|device| device.serial.as_str())
            != Some(serial.as_str())
        {
            return Err("所选设备与当前识别结果不一致，请重新检测".to_string());
        }
    }

    let started = mutate(&app, |snapshot| {
        snapshot.busy = true;
        snapshot.phase = RunPhase::HardwareTest;
        snapshot.progress = 0;
        snapshot.hardware_status = HardwareStatus::Running;
        snapshot.hardware_progress = 0;
        snapshot.hardware_tests = hardware::initial_results();
        snapshot.status_message = "正在执行自动硬件检测".to_string();
        push_event(snapshot, EventLevel::Info, "硬件检测已开始");
    })?;

    let app_handle = app.clone();
    thread::spawn(move || {
        for id in hardware::AUTOMATIC_TEST_IDS {
            let _ = mutate(&app_handle, |snapshot| {
                if let Some(test) = snapshot
                    .hardware_tests
                    .iter_mut()
                    .find(|test| test.id == *id)
                {
                    test.status = HardwareTestStatus::Running;
                    test.summary = "正在读取设备数据".to_string();
                }
            });

            let identity = app_handle
                .state::<FactoryState>()
                .snapshot
                .lock()
                .ok()
                .and_then(|snapshot| snapshot.selected_device.clone());
            let result = hardware::run_automatic(&serial, id, identity.as_ref());
            let passed = result.status == HardwareTestStatus::Passed;
            let label = result.label.clone();
            let detail = result.detail.clone();
            let _ = mutate(&app_handle, |snapshot| {
                if let Some(target) = snapshot
                    .hardware_tests
                    .iter_mut()
                    .find(|test| test.id == *id)
                {
                    *target = result;
                }
                snapshot.hardware_progress = hardware_progress(snapshot);
                snapshot.progress = snapshot.hardware_progress;
                snapshot.status_message = format!("正在检测：{label}");
                if !passed {
                    push_event(
                        snapshot,
                        EventLevel::Error,
                        format!(
                            "{label}异常：{}",
                            detail.unwrap_or_else(|| "检测未通过".to_string())
                        ),
                    );
                }
            });
        }

        let _ = mutate(&app_handle, |snapshot| {
            for test in &mut snapshot.hardware_tests {
                if test.kind == HardwareTestKind::Interactive {
                    test.status = HardwareTestStatus::NeedsConfirmation;
                }
            }
            snapshot.busy = false;
            let failed = snapshot
                .hardware_tests
                .iter()
                .filter(|test| test.kind == HardwareTestKind::Automatic)
                .filter(|test| test.status == HardwareTestStatus::Failed)
                .count();
            if failed == 0 {
                snapshot.hardware_status = HardwareStatus::NeedsConfirmation;
                snapshot.status_message = format!(
                    "自动检测通过，等待 {} 项人工确认",
                    hardware::INTERACTIVE_TEST_IDS.len()
                );
                push_event(
                    snapshot,
                    EventLevel::Success,
                    format!("{} 项自动检测全部通过", hardware::AUTOMATIC_TEST_IDS.len()),
                );
            } else {
                snapshot.hardware_status = HardwareStatus::Failed;
                snapshot.status_message = format!("自动检测发现 {failed} 项异常，仍可完成人工确认");
                push_event(
                    snapshot,
                    EventLevel::Error,
                    format!("自动检测发现 {failed} 项异常"),
                );
            }
        });
    });

    Ok(started)
}

#[tauri::command]
fn confirm_hardware_test(
    app: AppHandle,
    id: String,
    passed: bool,
) -> Result<StationSnapshot, String> {
    mutate(&app, |snapshot| {
        let Some(test) = snapshot
            .hardware_tests
            .iter_mut()
            .find(|test| test.id == id)
        else {
            return;
        };
        if test.kind != HardwareTestKind::Interactive || test.status == HardwareTestStatus::Running
        {
            return;
        }
        let label = test.label.clone();
        test.status = if passed {
            HardwareTestStatus::Passed
        } else {
            HardwareTestStatus::Failed
        };
        test.summary = if passed {
            "工人确认通过"
        } else {
            "工人确认异常"
        }
        .to_string();
        test.value = Some(if passed { "已确认" } else { "不通过" }.to_string());
        test.detail = (!passed).then(|| "需要检查硬件、装配或线材后重新检测".to_string());

        snapshot.hardware_progress = hardware_progress(snapshot);
        snapshot.progress = snapshot.hardware_progress;
        push_event(
            snapshot,
            if passed {
                EventLevel::Success
            } else {
                EventLevel::Error
            },
            format!(
                "{label}：{}",
                if passed {
                    "人工确认通过"
                } else {
                    "人工确认异常"
                }
            ),
        );

        let all_resolved = snapshot.hardware_tests.iter().all(|test| {
            matches!(
                test.status,
                HardwareTestStatus::Passed | HardwareTestStatus::Failed
            )
        });
        let failed = snapshot
            .hardware_tests
            .iter()
            .filter(|test| test.status == HardwareTestStatus::Failed)
            .count();
        if all_resolved {
            if failed == 0 {
                snapshot.hardware_status = HardwareStatus::Passed;
                snapshot.status_message = "硬件检测全部通过".to_string();
                push_event(snapshot, EventLevel::Success, "本机硬件检测通过");
            } else {
                snapshot.hardware_status = HardwareStatus::Failed;
                snapshot.status_message = format!("硬件检测存在 {failed} 项异常");
            }
        } else if failed > 0 {
            snapshot.hardware_status = HardwareStatus::Failed;
            snapshot.status_message = "硬件检测存在异常，等待完成其余确认".to_string();
        } else {
            snapshot.hardware_status = HardwareStatus::NeedsConfirmation;
            snapshot.status_message = "等待完成其余人工确认".to_string();
        }
    })
}

#[tauri::command]
fn play_hardware_test_tone(app: AppHandle, serial: String) -> Result<StationSnapshot, String> {
    {
        let state = app.state::<FactoryState>();
        let snapshot = state
            .snapshot
            .lock()
            .map_err(|_| "工位状态锁异常".to_string())?;
        if snapshot.busy {
            return Err("自动检测尚未结束".to_string());
        }
        if snapshot
            .selected_device
            .as_ref()
            .map(|device| device.serial.as_str())
            != Some(serial.as_str())
        {
            return Err("所选设备与当前识别结果不一致，请重新检测".to_string());
        }
        let speaker_ready = snapshot
            .hardware_tests
            .iter()
            .any(|test| test.id == "speaker" && test.status != HardwareTestStatus::Waiting);
        if !speaker_ready {
            return Err("请先完成自动硬件检测".to_string());
        }
    }

    let started = mutate(&app, |snapshot| {
        if let Some(test) = snapshot
            .hardware_tests
            .iter_mut()
            .find(|test| test.id == "speaker")
        {
            test.summary = "正在向设备播放双音测试音".to_string();
        }
        push_event(snapshot, EventLevel::Info, "正在播放扬声器测试音");
    })?;

    let app_handle = app.clone();
    thread::spawn(move || match hardware::play_test_tone(&serial) {
        Ok(()) => {
            let _ = mutate(&app_handle, |snapshot| {
                if let Some(test) = snapshot
                    .hardware_tests
                    .iter_mut()
                    .find(|test| test.id == "speaker")
                {
                    test.summary = "测试音已播放，请确认是否清晰无杂音".to_string();
                    test.detail = None;
                }
                push_event(
                    snapshot,
                    EventLevel::Info,
                    "扬声器测试音播放完成，等待人工确认",
                );
            });
        }
        Err(error) => {
            let _ = mutate(&app_handle, |snapshot| {
                if let Some(test) = snapshot
                    .hardware_tests
                    .iter_mut()
                    .find(|test| test.id == "speaker")
                {
                    test.summary = "无法播放测试音".to_string();
                    test.detail = Some(error.clone());
                }
                push_event(snapshot, EventLevel::Error, error.clone());
            });
        }
    });

    Ok(started)
}

#[tauri::command]
fn run_microphone_hardware_test(app: AppHandle, serial: String) -> Result<StationSnapshot, String> {
    {
        let state = app.state::<FactoryState>();
        let snapshot = state
            .snapshot
            .lock()
            .map_err(|_| "工位状态锁异常".to_string())?;
        if snapshot.busy {
            return Err("自动检测尚未结束".to_string());
        }
        if snapshot
            .selected_device
            .as_ref()
            .map(|device| device.serial.as_str())
            != Some(serial.as_str())
        {
            return Err("所选设备与当前识别结果不一致，请重新检测".to_string());
        }
        let microphone_status = snapshot
            .hardware_tests
            .iter()
            .find(|test| test.id == "microphone")
            .map(|test| test.status.clone());
        if microphone_status == Some(HardwareTestStatus::Running) {
            return Err("麦克风正在采集，请等待本次录音完成".to_string());
        }
        if microphone_status.is_none() || microphone_status == Some(HardwareTestStatus::Waiting) {
            return Err("请先完成自动硬件检测".to_string());
        }
    }

    let started = mutate(&app, |snapshot| {
        if let Some(test) = snapshot
            .hardware_tests
            .iter_mut()
            .find(|test| test.id == "microphone")
        {
            test.status = HardwareTestStatus::Running;
            test.summary = "正在录制 3 秒，请靠近设备正常说话".to_string();
            test.value = None;
            test.detail = Some("与涂鸦共用 default/CaptureDsnoop 真实采集链路".to_string());
            test.raw_output = None;
            test.duration_ms = None;
        }
        snapshot.hardware_progress = hardware_progress(snapshot);
        snapshot.progress = snapshot.hardware_progress;
        snapshot.hardware_status = HardwareStatus::Running;
        snapshot.status_message = "麦克风正在重新采集，请持续说话 3 秒".to_string();
        push_event(snapshot, EventLevel::Info, "麦克风开始采集，请持续说话 3 秒");
    })?;

    let app_handle = app.clone();
    thread::spawn(move || match hardware::run_microphone_test(&serial) {
        Ok(result) => {
            let passed_signal = result.status != HardwareTestStatus::Failed;
            let value = result.value.clone().unwrap_or_default();
            let _ = mutate(&app_handle, |snapshot| {
                if let Some(test) = snapshot
                    .hardware_tests
                    .iter_mut()
                    .find(|test| test.id == "microphone")
                {
                    *test = result;
                }
                snapshot.hardware_progress = hardware_progress(snapshot);
                snapshot.progress = snapshot.hardware_progress;
                if !passed_signal {
                    snapshot.hardware_status = HardwareStatus::Failed;
                    snapshot.status_message = "麦克风输入电平异常".to_string();
                } else if snapshot
                    .hardware_tests
                    .iter()
                    .any(|test| test.status == HardwareTestStatus::Failed)
                {
                    snapshot.hardware_status = HardwareStatus::Failed;
                    snapshot.status_message = "麦克风采集正常，但其他硬件项目仍有异常".to_string();
                } else {
                    snapshot.hardware_status = HardwareStatus::NeedsConfirmation;
                    snapshot.status_message = "麦克风已回放，等待人工确认音质".to_string();
                }
                push_event(
                    snapshot,
                    if passed_signal {
                        EventLevel::Success
                    } else {
                        EventLevel::Error
                    },
                    format!("麦克风采集完成：{value}"),
                );
            });
        }
        Err(error) => {
            let _ = mutate(&app_handle, |snapshot| {
                if let Some(test) = snapshot
                    .hardware_tests
                    .iter_mut()
                    .find(|test| test.id == "microphone")
                {
                    test.status = HardwareTestStatus::Failed;
                    test.summary = "真实麦克风录制失败".to_string();
                    test.detail = Some(error.clone());
                    test.raw_output = Some(format!("[stderr]\n{error}"));
                }
                snapshot.hardware_progress = hardware_progress(snapshot);
                snapshot.progress = snapshot.hardware_progress;
                snapshot.hardware_status = HardwareStatus::Failed;
                snapshot.status_message = "麦克风检测失败".to_string();
                push_event(snapshot, EventLevel::Error, error.clone());
            });
        }
    });

    Ok(started)
}

#[tauri::command]
fn start_demo_run(app: AppHandle) -> Result<StationSnapshot, String> {
    {
        let state = app.state::<FactoryState>();
        let snapshot = state
            .snapshot
            .lock()
            .map_err(|_| "工位状态锁异常".to_string())?;
        if snapshot.busy {
            return Err("当前流程仍在运行".to_string());
        }
    }
    let started = mutate(&app, |snapshot| {
        snapshot.busy = true;
        snapshot.phase = RunPhase::ReadingIdentity;
        snapshot.progress = 8;
        snapshot.status_message = "演示：正在读取设备身份".to_string();
        snapshot.selected_device = Some(DeviceIdentity {
            serial: "DEMO-A133-0007".to_string(),
            cpuid: Some("演示模式不显示真实CPUID".to_string()),
            firmware_version: Some("0.9.0-dev".to_string()),
            tuya_pid: Some("DEMO-PID".to_string()),
        });
        push_event(snapshot, EventLevel::Info, "演示流程已启动，不会写入设备");
    })?;

    if started.busy && started.phase == RunPhase::ReadingIdentity {
        let app_handle = app.clone();
        thread::spawn(move || {
            let stages = [
                (RunPhase::Preparing, 28, "演示：正在请求生产凭据"),
                (RunPhase::Writing, 46, "演示：正在安全写入"),
                (RunPhase::Verifying, 66, "演示：正在验证持久化"),
                (RunPhase::Validating, 82, "演示：正在验证云端服务"),
                (RunPhase::HardwareTest, 93, "演示：正在执行本地硬件测试"),
                (RunPhase::Passed, 100, "演示完成，整机测试通过"),
            ];
            for (phase, progress, message) in stages {
                thread::sleep(Duration::from_millis(620));
                let _ = mutate(&app_handle, |snapshot| {
                    snapshot.phase = phase;
                    snapshot.progress = progress;
                    snapshot.status_message = message.to_string();
                    if phase == RunPhase::Passed {
                        snapshot.busy = false;
                        push_event(snapshot, EventLevel::Success, "演示流程完成");
                    }
                });
            }
        });
    }
    Ok(started)
}

fn map_pipeline_level(level: EventLevelLite) -> EventLevel {
    match level {
        EventLevelLite::Info => EventLevel::Info,
        EventLevelLite::Success => EventLevel::Success,
        EventLevelLite::Warning => EventLevel::Warning,
        EventLevelLite::Error => EventLevel::Error,
    }
}

#[tauri::command]
fn import_tuya_licenses(app: AppHandle, path: String) -> Result<StationSnapshot, String> {
    let config = StationConfig::load();
    let store = LicenseStore::open(&config.data_dir)?;
    let report: ImportReport = store.import_file(&config.tuya_pid, &PathBuf::from(&path))?;
    mutate(&app, |snapshot| {
        snapshot.license_inventory = inventory_view(&config);
        snapshot.tuya_pid = config.tuya_pid.clone();
        push_event(
            snapshot,
            EventLevel::Success,
            format!(
                "License 导入完成：新增 {}，重复跳过 {}，失败 {}",
                report.imported, report.skipped_duplicate, report.failed
            ),
        );
        if !report.errors.is_empty() {
            push_event(
                snapshot,
                EventLevel::Warning,
                report.errors.into_iter().take(3).collect::<Vec<_>>().join("；"),
            );
        }
    })
}

#[tauri::command]
fn start_production_run(app: AppHandle, serial: String) -> Result<StationSnapshot, String> {
    let config = StationConfig::load();
    if !config.station_ready() {
        return Err("工位配置不完整：请配置 STATION_ID/BATCH_NUMBER/TUYA_PID 与签名私钥".to_string());
    }

    {
        let state = app.state::<FactoryState>();
        let snapshot = state
            .snapshot
            .lock()
            .map_err(|_| "工位状态锁异常".to_string())?;
        if snapshot.busy {
            return Err("当前设备仍在执行生产流程".to_string());
        }
        if snapshot
            .selected_device
            .as_ref()
            .map(|device| device.serial.as_str())
            != Some(serial.as_str())
        {
            return Err("所选设备与当前识别结果不一致，请重新检测".to_string());
        }
    }

    let started = mutate(&app, |snapshot| {
        snapshot.busy = true;
        snapshot.phase = RunPhase::ReadingIdentity;
        snapshot.progress = 6;
        snapshot.status_message = "正在启动本地凭据写入流程".to_string();
        snapshot.cloud_configured = config.station_ready();
        snapshot.config_checks = config.config_checks();
        snapshot.license_inventory = inventory_view(&config);
        push_event(
            snapshot,
            EventLevel::Info,
            "真实生产流程已启动（本地签名 + 涂鸦库存，无 Bash）",
        );
    })?;

    let app_handle = app.clone();
    thread::spawn(move || {
        let result = pipeline::run_credentials_pipeline(&serial, &config, &mut |ev| {
            let _ = mutate(&app_handle, |snapshot| {
                snapshot.phase = ev.phase;
                snapshot.progress = ev.progress;
                snapshot.status_message = ev.message.clone();
                push_event(snapshot, map_pipeline_level(ev.level), ev.message);
            });
        });

        let _ = mutate(&app_handle, |snapshot| {
            snapshot.busy = false;
            snapshot.license_inventory = inventory_view(&config);
            if result.ok {
                snapshot.phase = RunPhase::CredentialPassed;
                snapshot.progress = 100;
                snapshot.status_message = result.message.clone();
                push_event(snapshot, EventLevel::Success, result.message.clone());
                if result.secret_present {
                    push_event(snapshot, EventLevel::Success, "设备已生成 device_secret");
                }
            } else {
                snapshot.phase = RunPhase::Failed;
                snapshot.status_message = result.message.clone();
                push_event(
                    snapshot,
                    EventLevel::Error,
                    format!(
                        "{}{}",
                        result
                            .failure_code
                            .as_deref()
                            .map(|c| format!("[{c}] "))
                            .unwrap_or_default(),
                        result.message
                    ),
                );
            }
        });
    });

    Ok(started)
}

#[tauri::command]
fn reset_run(app: AppHandle) -> Result<StationSnapshot, String> {
    let mut result = scan_devices(app.clone())?;
    result.phase = RunPhase::Idle;
    result.progress = 0;
    result.busy = false;
    mutate(&app, |snapshot| {
        snapshot.phase = result.phase;
        snapshot.progress = result.progress;
        snapshot.busy = result.busy;
        snapshot.hardware_status = HardwareStatus::Idle;
        snapshot.hardware_progress = 0;
        snapshot.hardware_tests = hardware::initial_results();
        snapshot.status_message = if snapshot.selected_device.is_some() {
            "设备身份已读取".to_string()
        } else {
            "等待设备接入".to_string()
        };
        push_event(snapshot, EventLevel::Info, "工位已准备下一台设备");
    })
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(FactoryState {
            snapshot: Mutex::new(initial_snapshot()),
        })
        .setup(|app| {
            configure_bundled_adb(app)?;
            let state = app.state::<FactoryState>();
            if let Ok(mut snapshot) = state.snapshot.lock() {
                snapshot.adb_available = adb::available();
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            get_station_snapshot,
            scan_devices,
            start_hardware_tests,
            confirm_hardware_test,
            play_hardware_test_tone,
            run_microphone_hardware_test,
            start_demo_run,
            start_production_run,
            import_tuya_licenses,
            reset_run
        ])
        .run(tauri::generate_context!())
        .expect("failed to run AITVBOX factory assistant");
}
