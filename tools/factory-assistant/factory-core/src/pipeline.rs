//! Local production pipeline: sign + tuya license write + optional cloud wait.

use crate::adb;
use crate::license_store::{license_file_bytes, LicenseStore};
use crate::model::RunPhase;
use crate::signing::{self, sha256_hex};
use crate::station::StationConfig;
use std::thread;
use std::time::{Duration, Instant};
use uuid::Uuid;

#[derive(Debug, Clone)]
pub struct PipelineEvent {
    pub phase: RunPhase,
    pub progress: u8,
    pub message: String,
    pub level: EventLevelLite,
}

#[derive(Debug, Clone, Copy)]
pub enum EventLevelLite {
    Info,
    Success,
    Warning,
    Error,
}

#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PipelineResult {
    pub ok: bool,
    pub phase: String,
    pub cpuid: String,
    pub device_sig_sha256: Option<String>,
    pub license_uuid: Option<String>,
    pub license_file_sha256: Option<String>,
    pub secret_present: bool,
    pub failure_code: Option<String>,
    pub message: String,
}

pub fn run_credentials_pipeline(
    serial: &str,
    config: &StationConfig,
    on_event: &mut dyn FnMut(PipelineEvent),
) -> PipelineResult {
    let emit = |on_event: &mut dyn FnMut(PipelineEvent),
                phase: RunPhase,
                progress: u8,
                message: &str,
                level: EventLevelLite| {
        on_event(PipelineEvent {
            phase,
            progress,
            message: message.to_string(),
            level,
        });
    };

    let fail = |code: &str, msg: String| PipelineResult {
        ok: false,
        phase: "failed".into(),
        cpuid: String::new(),
        device_sig_sha256: None,
        license_uuid: None,
        license_file_sha256: None,
        secret_present: false,
        failure_code: Some(code.into()),
        message: msg,
    };

    if !config.station_ready() {
        return fail(
            "FAIL_STATION_CONFIG",
            "工位配置不完整：需要 STATION_ID/BATCH/TUYA_PID/私钥".into(),
        );
    }

    emit(
        on_event,
        RunPhase::ReadingIdentity,
        8,
        "正在读取设备身份",
        EventLevelLite::Info,
    );

    if let Err(e) = adb::ensure_root(serial) {
        return fail("FAIL_ADB", e);
    }

    let identity = match adb::inspect(serial) {
        Ok(i) => i,
        Err(e) => return fail("FAIL_ADB", e),
    };
    let Some(cpuid) = identity.cpuid.clone() else {
        return fail("FAIL_CPUID", "设备没有返回有效 CPUID".into());
    };
    // Read twice
    let identity2 = match adb::inspect(serial) {
        Ok(i) => i,
        Err(e) => return fail("FAIL_ADB", e),
    };
    if identity2.cpuid.as_deref() != Some(cpuid.as_str()) {
        return fail("FAIL_CPUID", "两次读取 CPUID 不一致".into());
    }

    let device_pid = identity.tuya_pid.clone();
    let effective_pid = match (&device_pid, config.default_tuya_pid_if_missing) {
        (Some(p), _) => {
            if p != &config.tuya_pid {
                return fail(
                    "FAIL_FIRMWARE",
                    format!(
                        "设备 PID={p} 与工位 TUYA_PID={} 不一致",
                        config.tuya_pid
                    ),
                );
            }
            p.clone()
        }
        (None, true) => {
            emit(
                on_event,
                RunPhase::ReadingIdentity,
                12,
                &format!(
                    "设备无 /etc/aitvbox-tuya-pid，使用工位 PID {}",
                    config.tuya_pid
                ),
                EventLevelLite::Warning,
            );
            config.tuya_pid.clone()
        }
        (None, false) => {
            return fail(
                "FAIL_FIRMWARE",
                "设备缺少 /etc/aitvbox-tuya-pid，请刷新固件".into(),
            );
        }
    };

    emit(
        on_event,
        RunPhase::Preparing,
        22,
        &format!(
            "CPUID={} 固件={:?}",
            &cpuid[..8.min(cpuid.len())],
            identity.firmware_version
        ),
        EventLevelLite::Info,
    );

    if let Err(e) = adb::wait_for_mount(serial, "/etc/100ask", 30) {
        return fail("FAIL_MOUNT", e);
    }
    if let Err(e) = adb::wait_for_mount(serial, "/factory/tuya", 30) {
        return fail("FAIL_MOUNT", e);
    }

    let production_id = Uuid::new_v4().to_string();
    let store = match LicenseStore::open(&config.data_dir) {
        Ok(s) => s,
        Err(e) => return fail("FAIL_LICENSE_BIND", e),
    };

    emit(
        on_event,
        RunPhase::Preparing,
        30,
        "正在签名 device_sig 并预占涂鸦 License",
        EventLevelLite::Info,
    );

    let device_sig = match signing::sign_cpuid_file(&config.signing_private_key, &cpuid) {
        Ok(s) => s,
        Err(e) => return fail("FAIL_SIGN", e),
    };
    if let Some(pub_path) = &config.signing_public_key {
        if let Ok(pem) = std::fs::read_to_string(pub_path) {
            match signing::verify_cpuid_sig(&pem, &cpuid, &device_sig) {
                Ok(true) => {}
                Ok(false) => {
                    return fail("FAIL_SIGN", "本地公钥验签失败".into());
                }
                Err(e) => {
                    emit(
                        on_event,
                        RunPhase::Preparing,
                        32,
                        &format!("公钥验签跳过: {e}"),
                        EventLevelLite::Warning,
                    );
                }
            }
        }
    }

    let license = match store.reserve_for_cpuid(&effective_pid, &cpuid, &production_id) {
        Ok(l) => l,
        Err(e) => {
            let code = if e.contains("FAIL_LICENSE_EMPTY") {
                "FAIL_LICENSE_EMPTY"
            } else {
                "FAIL_LICENSE_BIND"
            };
            return fail(code, e);
        }
    };

    emit(
        on_event,
        RunPhase::Writing,
        45,
        "正在原子写入 device_sig 与 license.env",
        EventLevelLite::Info,
    );

    if let Err(e) = adb::write_text_atomic(serial, "/etc/100ask/device_sig", &device_sig) {
        return fail("FAIL_WRITE_SIG", e);
    }
    let sig_read = match adb::read_remote_text(serial, "/etc/100ask/device_sig") {
        Ok(s) => s.trim().to_string(),
        Err(e) => return fail("FAIL_WRITE_SIG", e),
    };
    if sig_read != device_sig {
        return fail("FAIL_WRITE_SIG", "device_sig 读回不一致".into());
    }
    if adb::remote_file_mode(serial, "/etc/100ask/device_sig").ok().as_deref() != Some("600") {
        return fail("FAIL_WRITE_SIG", "device_sig 权限不是 600".into());
    }

    let license_body = license_file_bytes(&license.uuid, &license.auth_key);
    let license_hash = sha256_hex(license_body.as_bytes());
    if let Err(e) = adb::write_text_atomic(serial, "/factory/tuya/license.env", &license_body) {
        return fail("FAIL_WRITE_LICENSE", e);
    }
    // 设备 busybox 可能无 base64；文本路径用 cat 读回比对。
    let lic_read = match adb::read_remote_text(serial, "/factory/tuya/license.env") {
        Ok(s) => s.replace('\r', ""),
        Err(e) => return fail("FAIL_WRITE_LICENSE", e),
    };
    if lic_read != license_body && lic_read.trim_end_matches('\n') != license_body.trim_end_matches('\n') {
        // 允许末尾换行差异时仍按规范哈希比对
        if sha256_hex(lic_read.as_bytes()) != license_hash {
            return fail(
                "FAIL_WRITE_LICENSE",
                format!(
                    "license.env 读回不一致 (local_len={} remote_len={})",
                    license_body.len(),
                    lic_read.len()
                ),
            );
        }
    }
    if adb::remote_file_mode(serial, "/factory/tuya/license.env")
        .ok()
        .as_deref()
        != Some("600")
    {
        return fail("FAIL_WRITE_LICENSE", "license.env 权限不是 600".into());
    }

    let _ = store.mark_written(license.id);
    adb::kill_tuya_process(serial);

    if config.reboot_verify {
        emit(
            on_event,
            RunPhase::Verifying,
            60,
            "重启设备并复核持久化文件",
            EventLevelLite::Info,
        );
        if let Err(e) = adb::reboot(serial) {
            return fail("FAIL_REBOOT_VERIFY", e);
        }
        if let Err(e) = adb::wait_for_mount(serial, "/etc/100ask", 60) {
            return fail("FAIL_REBOOT_VERIFY", e);
        }
        if let Err(e) = adb::wait_for_mount(serial, "/factory/tuya", 60) {
            return fail("FAIL_REBOOT_VERIFY", e);
        }
        let sig2 = adb::read_remote_text(serial, "/etc/100ask/device_sig")
            .map(|s| s.trim().to_string())
            .unwrap_or_default();
        let lic2 = adb::read_remote_text(serial, "/factory/tuya/license.env")
            .unwrap_or_default()
            .replace('\r', "");
        if sig2 != device_sig {
            return fail("FAIL_REBOOT_VERIFY", "重启后 device_sig 不一致".into());
        }
        let lic_ok = lic2 == license_body
            || lic2.trim_end_matches('\n') == license_body.trim_end_matches('\n')
            || sha256_hex(lic2.as_bytes()) == license_hash;
        if !lic_ok {
            return fail("FAIL_REBOOT_VERIFY", "重启后 license.env 不一致".into());
        }
    } else {
        emit(
            on_event,
            RunPhase::Verifying,
            60,
            "写回校验通过（未启用重启复核）",
            EventLevelLite::Success,
        );
    }

    let mut secret_present = false;
    if config.wait_cloud {
        emit(
            on_event,
            RunPhase::Validating,
            75,
            &format!(
                "等待设备 provision（最多 {}s，需工厂网络）",
                config.credential_timeout_sec
            ),
            EventLevelLite::Info,
        );
        let deadline = Instant::now() + Duration::from_secs(config.credential_timeout_sec);
        while Instant::now() < deadline {
            if adb::remote_file_exists(serial, "/etc/100ask/secret") {
                secret_present = true;
                break;
            }
            thread::sleep(Duration::from_secs(2));
        }
        if !secret_present {
            emit(
                on_event,
                RunPhase::Validating,
                85,
                "超时未出现 secret：请确认 Wi-Fi 与 100ask 连通；文件已写入可复检",
                EventLevelLite::Warning,
            );
            // Credentials written OK; cloud wait soft-fail for old devices / offline
            return PipelineResult {
                ok: true,
                phase: "credentialPassed".into(),
                cpuid,
                device_sig_sha256: Some(sha256_hex(device_sig.as_bytes())),
                license_uuid: Some(license.uuid),
                license_file_sha256: Some(license_hash),
                secret_present: false,
                failure_code: Some("WARN_CLOUD_NOT_READY".into()),
                message: "凭据已写入并校验；云端 secret 未就绪（可连网后复检）".into(),
            };
        }
    }

    let _ = store.mark_shipped(license.id);
    emit(
        on_event,
        RunPhase::CredentialPassed,
        95,
        "凭据写入完成",
        EventLevelLite::Success,
    );

    PipelineResult {
        ok: true,
        phase: "credentialPassed".into(),
        cpuid,
        device_sig_sha256: Some(sha256_hex(device_sig.as_bytes())),
        license_uuid: Some(mask_uuid(&license.uuid)),
        license_file_sha256: Some(license_hash),
        secret_present,
        failure_code: None,
        message: if secret_present {
            "凭据通过：device_sig + License 已写，secret 已生成".into()
        } else {
            "凭据写入完成".into()
        },
    }
}

fn mask_uuid(uuid: &str) -> String {
    if uuid.len() <= 8 {
        return uuid.to_string();
    }
    format!("{}…{}", &uuid[..4], &uuid[uuid.len() - 4..])
}
