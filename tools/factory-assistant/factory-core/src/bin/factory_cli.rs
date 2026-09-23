use aitvbox_factory_core::license_store::LicenseStore;
use aitvbox_factory_core::model::{DeviceIdentity, DeviceSummary, HardwareTestResult};
use aitvbox_factory_core::pipeline::{self, EventLevelLite};
use aitvbox_factory_core::station::StationConfig;
use aitvbox_factory_core::{adb, hardware};
use serde::Serialize;
use std::{env, path::PathBuf, process};

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct ScanResult {
    adb_available: bool,
    devices: Vec<DeviceSummary>,
    selected_device: Option<DeviceIdentity>,
    message: String,
    error: Option<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct HardwareResult {
    tests: Vec<HardwareTestResult>,
}

fn scan() -> Result<ScanResult, String> {
    let adb_available = adb::available();
    if !adb_available {
        return Ok(ScanResult {
            adb_available,
            devices: Vec::new(),
            selected_device: None,
            message: "未找到 ADB 服务".to_string(),
            error: Some("主机无法调用 adb".to_string()),
        });
    }

    let devices = adb::scan()?;
    let usable: Vec<_> = devices
        .iter()
        .filter(|device| device.state == "device")
        .collect();
    let (selected_device, message, error) = match usable.as_slice() {
        [] => (
            None,
            "等待设备接入".to_string(),
            Some("没有发现可用的 ADB 设备".to_string()),
        ),
        [device] => match adb::inspect(&device.serial) {
            Ok(identity) if identity.cpuid.is_some() => {
                (Some(identity), "设备身份已读取".to_string(), None)
            }
            Ok(_) => (
                None,
                "设备身份读取失败".to_string(),
                Some("设备没有返回有效 CPUID".to_string()),
            ),
            Err(error) => (None, "设备身份读取失败".to_string(), Some(error)),
        },
        _ => (
            None,
            "检测到多台设备".to_string(),
            Some("当前工位只允许连接一台 ADB 设备".to_string()),
        ),
    };

    Ok(ScanResult {
        adb_available,
        devices,
        selected_device,
        message,
        error,
    })
}

fn only_serial() -> Result<String, String> {
    let devices = adb::scan()?;
    let usable: Vec<_> = devices
        .into_iter()
        .filter(|d| d.state == "device")
        .collect();
    match usable.as_slice() {
        [d] => Ok(d.serial.clone()),
        [] => Err("没有 ADB 设备".into()),
        _ => Err("检测到多台 ADB 设备，请指定 serial".into()),
    }
}

fn import_licenses(path_arg: Option<String>) -> Result<String, String> {
    let config = StationConfig::load();
    let path = if let Some(p) = path_arg {
        PathBuf::from(p)
    } else {
        config.latest_import_file().ok_or_else(|| {
            format!(
                "未指定文件，且 {} 下没有 xlsx/csv。请把涂鸦采购表放入 imports/ 后重试",
                config.imports_dir.display()
            )
        })?
    };
    let store = LicenseStore::open(&config.data_dir)?;
    let report = store.import_file(&config.tuya_pid, &path)?;
    let summary = store.summary(&config.tuya_pid)?;
    serde_json::to_string_pretty(&serde_json::json!({
        "importedFrom": path,
        "import": report,
        "inventory": summary,
        "tuyaPid": config.tuya_pid,
        "dataDir": config.data_dir,
        "licenseDb": config.license_db_path(),
    }))
    .map_err(|e| e.to_string())
}

fn inventory() -> Result<String, String> {
    let config = StationConfig::load();
    let store = LicenseStore::open(&config.data_dir)?;
    let summary = store.summary(&config.tuya_pid)?;
    serde_json::to_string_pretty(&serde_json::json!({
        "inventory": summary,
        "tuyaPid": config.tuya_pid,
        "stationReady": config.station_ready(),
        "signingKey": config.signing_private_key,
        "factoryHome": config.factory_home,
        "dataDir": config.data_dir,
        "licenseDb": config.license_db_path(),
        "importsDir": config.imports_dir,
    }))
    .map_err(|e| e.to_string())
}

fn provision(serial: Option<String>) -> Result<String, String> {
    let config = StationConfig::load();
    if !config.station_ready() {
        return Err(format!(
            "工位未就绪：私钥={} 是否存在？",
            config.signing_private_key.display()
        ));
    }
    let serial = match serial {
        Some(s) => s,
        None => only_serial()?,
    };
    let mut logs = Vec::new();
    let result = pipeline::run_credentials_pipeline(&serial, &config, &mut |ev| {
        let tag = match ev.level {
            EventLevelLite::Info => "INFO",
            EventLevelLite::Success => "OK",
            EventLevelLite::Warning => "WARN",
            EventLevelLite::Error => "ERR",
        };
        eprintln!("[{tag}] ({}) {}", ev.progress, ev.message);
        logs.push(format!("[{tag}] {}", ev.message));
    });
    serde_json::to_string_pretty(&serde_json::json!({
        "result": result,
        "logs": logs,
        "serial": serial,
    }))
    .map_err(|e| e.to_string())
}

fn run_hardware(serial: &str) -> Result<HardwareResult, String> {
    let identity = adb::inspect(serial)?;
    if identity.cpuid.is_none() {
        return Err("设备没有返回有效 CPUID".to_string());
    }
    let tests = hardware::AUTOMATIC_TEST_IDS
        .iter()
        .map(|id| hardware::run_automatic(serial, id, Some(&identity)))
        .collect();
    Ok(HardwareResult { tests })
}

fn execute() -> Result<String, String> {
    let mut args = env::args().skip(1);
    match args.next().as_deref() {
        Some("scan") => serde_json::to_string(&scan()?).map_err(|e| e.to_string()),
        Some("hardware") => {
            let serial = args.next().map(Ok).unwrap_or_else(only_serial)?;
            serde_json::to_string(&run_hardware(&serial)?).map_err(|e| e.to_string())
        }
        Some("import") => {
            // 无参数：自动用 station/imports 下最新 xlsx
            import_licenses(args.next())
        }
        Some("inventory") => inventory(),
        Some("provision") => provision(args.next()),
        Some("tone") => {
            let serial = args.next().map(Ok).unwrap_or_else(only_serial)?;
            hardware::play_test_tone(&serial)?;
            Ok("{\"ok\":true}".to_string())
        }
        Some("microphone") => {
            let serial = args.next().map(Ok).unwrap_or_else(only_serial)?;
            serde_json::to_string(&hardware::run_microphone_test(&serial)?)
                .map_err(|e| e.to_string())
        }
        _ => Err(
            "用法:\n  factory_cli import [xlsx]   # 省略则用 station/imports 最新文件\n  factory_cli inventory\n  factory_cli provision [serial]  # 省略则用当前唯一 ADB 设备\n  factory_cli scan|hardware|..."
                .into(),
        ),
    }
}

fn main() {
    match execute() {
        Ok(json) => println!("{json}"),
        Err(error) => {
            eprintln!("{error}");
            process::exit(1);
        }
    }
}
