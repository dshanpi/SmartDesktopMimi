//! Station configuration — fixed defaults inside the factory-assistant package.
//!
//! 日常产线只需：把涂鸦 xlsx 放进 station/imports/ 再点生产。
//! 私钥、PID、数据目录均为工程内固定约定，无需每次 export / cp。
//!
//! ```text
//! tools/factory-assistant/station/
//!   keys/100ask_ecdsa.pem     ← 从受控工程包或显式私有 SDK 路径初始化
//!   data/tuya_licenses.sqlite
//!   imports/*.xlsx            ← 管理员放入采购表，执行 import
//!   logs/
//! ```

use crate::model::ConfigCheck;
use std::env;
use std::path::{Path, PathBuf};

/// 产品固定涂鸦 PID（与固件 CONFIG / /etc/aitvbox-tuya-pid 一致）。
pub const DEFAULT_TUYA_PID: &str = "alon7qgyjj8yus74";

#[derive(Debug, Clone)]
pub struct StationConfig {
    pub station_id: String,
    pub batch_number: String,
    pub tuya_pid: String,
    pub factory_home: PathBuf,
    pub signing_private_key: PathBuf,
    pub signing_public_key: Option<PathBuf>,
    pub data_dir: PathBuf,
    pub imports_dir: PathBuf,
    pub logs_dir: PathBuf,
    pub credential_timeout_sec: u64,
    pub reboot_verify: bool,
    pub wait_cloud: bool,
    pub default_tuya_pid_if_missing: bool,
}

impl StationConfig {
    pub fn load() -> Self {
        let factory_home = resolve_factory_home();
        ensure_layout(&factory_home);
        // 私钥是受控资产：仅从工程包或显式私有 SDK 路径拷入 station/keys。
        ensure_keys_seeded(&factory_home);

        let signing_private_key = env::var("SIGNING_PRIVATE_KEY_PATH")
            .map(PathBuf::from)
            .unwrap_or_else(|_| factory_home.join("keys").join("100ask_ecdsa.pem"));

        let default_pub = factory_home.join("keys").join("100ask_ecdsa.pub");
        let signing_public_key = env::var("SIGNING_PUBLIC_KEY_PATH")
            .map(PathBuf::from)
            .ok()
            .or_else(|| default_pub.is_file().then_some(default_pub));

        let data_dir = env::var("FACTORY_DATA_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|_| factory_home.join("data"));
        let _ = std::fs::create_dir_all(&data_dir);

        // 产线默认不联网等 secret；需要时再 WAIT_CLOUD=1
        let wait_cloud = env::var("WAIT_CLOUD")
            .map(|v| v == "1" || v.eq_ignore_ascii_case("true"))
            .unwrap_or(false);

        Self {
            station_id: env::var("STATION_ID").unwrap_or_else(|_| "station-1".to_string()),
            batch_number: env::var("BATCH_NUMBER").unwrap_or_else(|_| "default".to_string()),
            tuya_pid: env::var("TUYA_PID").unwrap_or_else(|_| DEFAULT_TUYA_PID.to_string()),
            factory_home: factory_home.clone(),
            signing_private_key,
            signing_public_key,
            data_dir,
            imports_dir: factory_home.join("imports"),
            logs_dir: factory_home.join("logs"),
            credential_timeout_sec: env::var("CREDENTIAL_TIMEOUT_SEC")
                .ok()
                .and_then(|v| v.parse().ok())
                .unwrap_or(180),
            reboot_verify: env::var("REBOOT_VERIFY")
                .map(|v| v == "1" || v.eq_ignore_ascii_case("true"))
                .unwrap_or(false),
            wait_cloud,
            default_tuya_pid_if_missing: env::var("ALLOW_MISSING_DEVICE_PID")
                .map(|v| v != "0")
                .unwrap_or(true),
        }
    }

    pub fn config_checks(&self) -> Vec<ConfigCheck> {
        let data_ok =
            self.data_dir.exists() || std::fs::create_dir_all(&self.data_dir).is_ok();
        vec![
            ConfigCheck {
                name: "STATION_ID".into(),
                configured: !self.station_id.trim().is_empty(),
                sensitive: false,
            },
            ConfigCheck {
                name: "BATCH_NUMBER".into(),
                configured: !self.batch_number.trim().is_empty(),
                sensitive: false,
            },
            ConfigCheck {
                name: "TUYA_PID".into(),
                configured: self.tuya_pid.len() == 16
                    && self.tuya_pid.chars().all(|c| c.is_ascii_alphanumeric()),
                sensitive: false,
            },
            ConfigCheck {
                name: "SIGNING_PRIVATE_KEY_PATH".into(),
                configured: self.signing_private_key.is_file(),
                sensitive: true,
            },
            ConfigCheck {
                name: "FACTORY_HOME".into(),
                configured: self.factory_home.exists(),
                sensitive: false,
            },
            ConfigCheck {
                name: "FACTORY_DATA_DIR".into(),
                configured: data_ok,
                sensitive: false,
            },
        ]
    }

    pub fn station_ready(&self) -> bool {
        self.config_checks().iter().all(|c| c.configured)
            && self.tuya_pid.len() == 16
            && self.signing_private_key.is_file()
    }

    pub fn license_db_path(&self) -> PathBuf {
        self.data_dir.join("tuya_licenses.sqlite")
    }

    /// station/imports 下最新的 xlsx/csv（按修改时间）。
    pub fn latest_import_file(&self) -> Option<PathBuf> {
        let dir = &self.imports_dir;
        let rd = std::fs::read_dir(dir).ok()?;
        let mut best: Option<(std::time::SystemTime, PathBuf)> = None;
        for ent in rd.flatten() {
            let path = ent.path();
            let ext = path
                .extension()
                .and_then(|e| e.to_str())
                .unwrap_or("")
                .to_ascii_lowercase();
            if !matches!(ext.as_str(), "xlsx" | "xls" | "csv" | "env" | "txt") {
                continue;
            }
            let modified = ent
                .metadata()
                .and_then(|m| m.modified())
                .unwrap_or(std::time::SystemTime::UNIX_EPOCH);
            if best.as_ref().map(|(t, _)| modified > *t).unwrap_or(true) {
                best = Some((modified, path));
            }
        }
        best.map(|(_, p)| p)
    }
}

pub fn resolve_factory_home() -> PathBuf {
    if let Ok(p) = env::var("AITVBOX_FACTORY_HOME") {
        return PathBuf::from(p);
    }
    if let Ok(p) = env::var("FACTORY_HOME") {
        return PathBuf::from(p);
    }

    if let Ok(exe) = env::current_exe() {
        if let Some(dir) = exe.parent() {
            if is_cargo_target_dir(dir) {
                if let Some(pkg_station) = package_station_dir() {
                    return pkg_station;
                }
            }
            let portable = dir.join("station");
            if portable.exists() || can_create_dir(&portable) {
                return portable;
            }
        }
    }

    if let Some(pkg_station) = package_station_dir() {
        return pkg_station;
    }

    env::current_dir()
        .map(|c| c.join("station"))
        .unwrap_or_else(|_| PathBuf::from("station"))
}

fn is_cargo_target_dir(dir: &Path) -> bool {
    let s = dir.to_string_lossy();
    s.contains("/target/") || s.contains("\\target\\")
}

fn package_station_dir() -> Option<PathBuf> {
    let from_crate = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..").join("station");
    if let Ok(canon) = from_crate.canonicalize() {
        return Some(canon);
    }
    if can_create_dir(&from_crate) {
        return Some(from_crate);
    }
    None
}

fn can_create_dir(path: &Path) -> bool {
    if path.exists() {
        return path.is_dir();
    }
    std::fs::create_dir_all(path).is_ok()
}

fn ensure_layout(home: &Path) {
    for sub in ["keys", "data", "imports", "logs"] {
        let _ = std::fs::create_dir_all(home.join(sub));
    }
}

/// 固定密钥：从受控输入拷到 station/keys（仅当目标不存在时）。
fn ensure_keys_seeded(factory_home: &Path) {
    let dest_pem = factory_home.join("keys").join("100ask_ecdsa.pem");
    let dest_pub = factory_home.join("keys").join("100ask_ecdsa.pub");
    if dest_pem.is_file() {
        return;
    }
    for src in fixed_private_key_candidates() {
        if src.is_file() {
            if let Some(parent) = dest_pem.parent() {
                let _ = std::fs::create_dir_all(parent);
            }
            if std::fs::copy(&src, &dest_pem).is_ok() {
                // 同目录公钥
                if let Some(dir) = src.parent() {
                    let pub_src = dir.join("100ask_ecdsa.pub");
                    if pub_src.is_file() && !dest_pub.is_file() {
                        let _ = std::fs::copy(pub_src, &dest_pub);
                    }
                    let der = dir.join("100ask_ecdsa_pub.der");
                    let dest_der = factory_home.join("keys").join("100ask_ecdsa_pub.der");
                    if der.is_file() && !dest_der.is_file() {
                        let _ = std::fs::copy(der, dest_der);
                    }
                }
                break;
            }
        }
    }
}

fn fixed_private_key_candidates() -> Vec<PathBuf> {
    let mut v = Vec::new();
    // 受控工程包使用英文 keys/ 目录；禁止在代码中硬编码中文路径。
    if let Ok(key_dir) = env::var("AITVBOX_FACTORY_KEY_DIR") {
        v.push(PathBuf::from(key_dir).join("100ask_ecdsa.pem"));
    }
    if let Ok(sdk) = env::var("AITVBOX_100ASK_SDK_ROOT") {
        v.push(PathBuf::from(sdk).join("100ask_keys/100ask_ecdsa.pem"));
    }
    if let Ok(pkg) = env::var("AITVBOX_FACTORY_PACKAGE") {
        v.push(PathBuf::from(pkg).join("keys/100ask_ecdsa.pem"));
    }
    v
}

pub fn package_root() -> PathBuf {
    resolve_factory_home()
}

pub fn resolve_existing(path: &Path) -> bool {
    path.is_file()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_home_is_package_station_not_xdg() {
        let home = resolve_factory_home();
        let s = home.to_string_lossy();
        assert!(
            !s.contains(".local/share"),
            "must not use XDG by default: {s}"
        );
        assert!(
            s.contains("station") || env::var_os("AITVBOX_FACTORY_HOME").is_some(),
            "expected package-local station: {s}"
        );
    }
}
