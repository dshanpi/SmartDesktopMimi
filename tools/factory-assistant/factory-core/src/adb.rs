use crate::model::{DeviceIdentity, DeviceSummary};
use std::{
    env,
    io::Read,
    path::Path,
    process::{Command, Stdio},
    thread,
    time::{Duration, Instant},
};

fn adb_binary() -> String {
    env::var("AITVBOX_ADB_PATH").unwrap_or_else(|_| "adb".to_string())
}

fn adb_command() -> Command {
    #[cfg(target_os = "windows")]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        let mut command = Command::new(adb_binary());
        command.creation_flags(CREATE_NO_WINDOW);
        command
    }
    #[cfg(not(target_os = "windows"))]
    {
        Command::new(adb_binary())
    }
}

fn run_adb(args: &[&str]) -> Result<String, String> {
    let mut child = adb_command()
        .args(args)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|error| format!("无法启动 adb: {error}"))?;

    let timeout_seconds = env::var("AITVBOX_ADB_TIMEOUT_SEC")
        .ok()
        .and_then(|value| value.parse::<u64>().ok())
        .unwrap_or(15);
    let deadline = Instant::now() + Duration::from_secs(timeout_seconds);
    let status = loop {
        match child.try_wait() {
            Ok(Some(status)) => break status,
            Ok(None) if Instant::now() < deadline => thread::sleep(Duration::from_millis(25)),
            Ok(None) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(format!("ADB 命令超过 {timeout_seconds} 秒未响应"));
            }
            Err(error) => {
                let _ = child.kill();
                return Err(format!("无法等待 adb 命令: {error}"));
            }
        }
    };

    let mut stdout = Vec::new();
    let mut stderr = Vec::new();
    if let Some(mut pipe) = child.stdout.take() {
        let _ = pipe.read_to_end(&mut stdout);
    }
    if let Some(mut pipe) = child.stderr.take() {
        let _ = pipe.read_to_end(&mut stderr);
    }

    if !status.success() {
        let detail = String::from_utf8_lossy(&stderr).trim().to_string();
        return Err(if detail.is_empty() {
            format!("adb 命令失败，退出码 {:?}", status.code())
        } else {
            detail
        });
    }
    Ok(String::from_utf8_lossy(&stdout).replace('\r', ""))
}

pub fn available() -> bool {
    adb_command()
        .arg("version")
        .output()
        .map(|output| output.status.success())
        .unwrap_or(false)
}

fn parse_devices(output: &str) -> Vec<DeviceSummary> {
    let mut devices = Vec::new();
    for line in output
        .lines()
        .skip(1)
        .map(str::trim)
        .filter(|line| !line.is_empty())
    {
        let mut fields = line.split_whitespace();
        let Some(serial) = fields.next() else {
            continue;
        };
        let state = fields.next().unwrap_or("unknown").to_string();
        let mut model = None;
        let mut product = None;
        let mut transport_id = None;

        for field in fields {
            if let Some(value) = field.strip_prefix("model:") {
                model = Some(value.to_string());
            } else if let Some(value) = field.strip_prefix("product:") {
                product = Some(value.to_string());
            } else if let Some(value) = field.strip_prefix("transport_id:") {
                transport_id = Some(value.to_string());
            }
        }

        devices.push(DeviceSummary {
            serial: serial.to_string(),
            state,
            model,
            product,
            transport_id,
        });
    }
    devices
}

pub fn scan() -> Result<Vec<DeviceSummary>, String> {
    run_adb(&["devices", "-l"]).map(|output| parse_devices(&output))
}

pub fn shell(serial: &str, command: &str) -> Result<String, String> {
    if !valid_serial(serial) {
        return Err("ADB 序列号格式不安全".to_string());
    }
    run_adb(&["-s", serial, "shell", command]).map(|value| value.trim().to_string())
}

pub fn push_file(serial: &str, local: &Path, remote: &str) -> Result<(), String> {
    if !valid_serial(serial) {
        return Err("ADB 序列号格式不安全".to_string());
    }
    if !remote.starts_with("/tmp/")
        || !remote
            .chars()
            .all(|character| character.is_ascii_alphanumeric() || "/._-".contains(character))
    {
        return Err("ADB 临时文件路径格式不安全".to_string());
    }
    let local = local
        .to_str()
        .ok_or_else(|| "本地临时文件路径不是有效 UTF-8".to_string())?;
    run_adb(&["-s", serial, "push", local, remote]).map(|_| ())
}

pub fn pull_file(serial: &str, remote: &str, local: &Path) -> Result<(), String> {
    if !valid_serial(serial) {
        return Err("ADB 序列号格式不安全".to_string());
    }
    if !remote.starts_with("/tmp/")
        || !remote
            .chars()
            .all(|character| character.is_ascii_alphanumeric() || "/._-".contains(character))
    {
        return Err("ADB 临时文件路径格式不安全".to_string());
    }
    let local = local
        .to_str()
        .ok_or_else(|| "本地临时文件路径不是有效 UTF-8".to_string())?;
    run_adb(&["-s", serial, "pull", remote, local]).map(|_| ())
}

fn valid_serial(serial: &str) -> bool {
    !serial.is_empty()
        && serial
            .chars()
            .all(|character| character.is_ascii_alphanumeric() || "-_:".contains(character))
}

fn parse_cpuid(sys_info: &str) -> Option<String> {
    sys_info.lines().find_map(|line| {
        let (name, value) = line.split_once(':')?;
        if name.trim().eq_ignore_ascii_case("sunxi_serial") {
            let normalized = value.trim().to_ascii_lowercase();
            (normalized.len() == 32 && normalized.chars().all(|item| item.is_ascii_hexdigit()))
                .then_some(normalized)
        } else {
            None
        }
    })
}

pub fn inspect(serial: &str) -> Result<DeviceIdentity, String> {
    if !valid_serial(serial) {
        return Err("ADB 序列号格式不安全".to_string());
    }

    run_adb(&["-s", serial, "wait-for-device"])?;

    let sys_info = shell(serial, "cat /sys/class/sunxi_info/sys_info")?;
    let cpuid = parse_cpuid(&sys_info);
    let firmware_version = shell(
        serial,
        "sed -n 's/^AITVBOX_VERSION=//p' /etc/aitvbox-version | head -1",
    )
    .ok()
    .filter(|value| !value.is_empty());
    let tuya_pid = shell(
        serial,
        "sed -n 's/^TUYA_PID=//p' /etc/aitvbox-tuya-pid 2>/dev/null | head -1",
    )
    .ok()
    .filter(|value| {
        value.len() == 16
            && value
                .chars()
                .all(|character| character.is_ascii_alphanumeric())
    });

    Ok(DeviceIdentity {
        serial: serial.to_string(),
        cpuid,
        firmware_version,
        tuya_pid,
    })
}

pub fn ensure_root(serial: &str) -> Result<(), String> {
    if !valid_serial(serial) {
        return Err("ADB 序列号格式不安全".to_string());
    }
    let _ = run_adb(&["-s", serial, "root"]);
    run_adb(&["-s", serial, "wait-for-device"])?;
    let id = shell(serial, "id -u")?;
    if id.trim() != "0" {
        return Err("adb root 后仍非 root，无法写入出厂凭据".to_string());
    }
    Ok(())
}

pub fn wait_for_mount(serial: &str, mountpoint: &str, timeout_sec: u64) -> Result<(), String> {
    if !mountpoint.starts_with('/')
        || !mountpoint
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "/._-".contains(c))
    {
        return Err("挂载点路径不安全".to_string());
    }
    let deadline = Instant::now() + Duration::from_secs(timeout_sec);
    while Instant::now() < deadline {
        let found = shell(
            serial,
            &format!(
                "awk '$5==\"{mountpoint}\" {{ found=1 }} END {{ exit(found ? 0 : 1) }}' /proc/self/mountinfo"
            ),
        );
        if found.is_ok() {
            return Ok(());
        }
        thread::sleep(Duration::from_millis(500));
    }
    Err(format!(
        "{mountpoint} 未 bind 到持久化分区，拒绝写入（等待 {timeout_sec}s 超时）"
    ))
}

pub fn remote_file_mode(serial: &str, path: &str) -> Result<String, String> {
    // Tina/busybox 可能没有 stat -c；用 ls -l 解析权限。
    let listing = shell(serial, &format!("ls -l '{path}' 2>/dev/null"))?;
    let mode_field = listing
        .split_whitespace()
        .next()
        .ok_or_else(|| format!("无法解析权限: {listing}"))?;
    // -rw------- -> 600
    let chars: Vec<char> = mode_field.chars().collect();
    if chars.len() < 10 {
        return Err(format!("权限字段异常: {mode_field}"));
    }
    let bit = |i: usize, expected: char| -> u32 {
        if chars.get(i) == Some(&expected) {
            1
        } else {
            0
        }
    };
    let owner = bit(1, 'r') * 4 + bit(2, 'w') * 2 + bit(3, 'x');
    let group = bit(4, 'r') * 4 + bit(5, 'w') * 2 + bit(6, 'x');
    let other = bit(7, 'r') * 4 + bit(8, 'w') * 2 + bit(9, 'x');
    Ok(format!("{owner}{group}{other}"))
}

pub fn remote_file_owner(serial: &str, path: &str) -> Result<String, String> {
    // busybox 无 GNU stat 时，root 写入即可；尽力解析 ls -ln
    let listing = shell(serial, &format!("ls -ln '{path}' 2>/dev/null"))?;
    let parts: Vec<&str> = listing.split_whitespace().collect();
    // mode links uid gid size ...
    if parts.len() >= 4 {
        Ok(format!("{}:{}", parts[2], parts[3]))
    } else {
        Ok("0:0".to_string())
    }
}

pub fn remote_file_exists(serial: &str, path: &str) -> bool {
    shell(serial, &format!("test -f '{path}' && echo yes"))
        .map(|v| v.trim() == "yes")
        .unwrap_or(false)
}

pub fn read_remote_bytes(serial: &str, path: &str) -> Result<Vec<u8>, String> {
    if !valid_serial(serial) {
        return Err("ADB 序列号格式不安全".to_string());
    }
    if !path.starts_with('/')
        || !path
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "/._-".contains(c))
    {
        return Err("远程路径不安全".to_string());
    }
    // 部分 A133 adbd 对 exec-out 不稳定；用 base64 经 shell 传输更稳妥。
    let b64 = shell(serial, &format!("base64 '{path}' 2>/dev/null"))?;
    let cleaned: String = b64.chars().filter(|c| !c.is_whitespace()).collect();
    if cleaned.is_empty() {
        return Err(format!("读取远程文件失败（空）: {path}"));
    }
    base64_decode(&cleaned).map_err(|e| format!("base64 解码失败 {path}: {e}"))
}

pub fn read_remote_text(serial: &str, path: &str) -> Result<String, String> {
    // 文本文件优先 shell cat（device_sig / license.env 均为 ASCII）
    let text = shell(serial, &format!("cat '{path}'"))?;
    Ok(text.replace('\r', ""))
}

fn base64_decode(input: &str) -> Result<Vec<u8>, String> {
    fn val(c: u8) -> Option<u8> {
        match c {
            b'A'..=b'Z' => Some(c - b'A'),
            b'a'..=b'z' => Some(c - b'a' + 26),
            b'0'..=b'9' => Some(c - b'0' + 52),
            b'+' => Some(62),
            b'/' => Some(63),
            b'=' => Some(0),
            _ => None,
        }
    }
    let bytes = input.as_bytes();
    if bytes.len() % 4 != 0 {
        return Err("长度不是 4 的倍数".into());
    }
    let mut out = Vec::with_capacity(bytes.len() / 4 * 3);
    for chunk in bytes.chunks(4) {
        let a = val(chunk[0]).ok_or("非法 base64")?;
        let b = val(chunk[1]).ok_or("非法 base64")?;
        let c = val(chunk[2]).ok_or("非法 base64")?;
        let d = val(chunk[3]).ok_or("非法 base64")?;
        out.push((a << 2) | (b >> 4));
        if chunk[2] != b'=' {
            out.push((b << 4) | (c >> 2));
        }
        if chunk[3] != b'=' {
            out.push((c << 6) | d);
        }
    }
    Ok(out)
}

/// Atomically write UTF-8 text to a remote path under a persisted directory.
/// Uses host temp + adb push to /tmp + shell mv into place (0600, root).
pub fn write_text_atomic(serial: &str, remote_path: &str, content: &str) -> Result<(), String> {
    if !valid_serial(serial) {
        return Err("ADB 序列号格式不安全".to_string());
    }
    if !(remote_path.starts_with("/etc/100ask/") || remote_path.starts_with("/factory/tuya/"))
        || !remote_path
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "/._-".contains(c))
    {
        return Err(format!("不允许写入路径: {remote_path}"));
    }
    let parent = remote_path
        .rsplit_once('/')
        .map(|(p, _)| p)
        .unwrap_or("/");
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    let local_tmp = env::temp_dir().join(format!("aitvbox_factory_{stamp}.tmp"));
    std::fs::write(&local_tmp, content.as_bytes())
        .map_err(|e| format!("写本地临时文件失败: {e}"))?;
    let remote_tmp = format!("/tmp/aitvbox_factory_{stamp}.tmp");
    let result = (|| {
        push_file(serial, &local_tmp, &remote_tmp)?;
        let remote_stage = format!("{parent}/.aitvbox_write_{stamp}.tmp");
        // parent 700 for tuya dir if needed; 100ask usually 755
        let prep = if parent == "/factory/tuya" {
            format!("mkdir -p '{parent}' && chmod 700 '{parent}'")
        } else {
            format!("mkdir -p '{parent}'")
        };
        shell(serial, &prep)?;
        shell(
            serial,
            &format!(
                "umask 077 && cp '{remote_tmp}' '{remote_stage}' && chmod 600 '{remote_stage}' && mv '{remote_stage}' '{remote_path}' && sync && rm -f '{remote_tmp}'"
            ),
        )?;
        Ok(())
    })();
    let _ = std::fs::remove_file(&local_tmp);
    result
}

pub fn reboot(serial: &str) -> Result<(), String> {
    if !valid_serial(serial) {
        return Err("ADB 序列号格式不安全".to_string());
    }
    let _ = run_adb(&["-s", serial, "reboot"]);
    // Wait for device to drop then come back
    thread::sleep(Duration::from_secs(3));
    let deadline = Instant::now() + Duration::from_secs(120);
    while Instant::now() < deadline {
        if run_adb(&["-s", serial, "wait-for-device"]).is_ok() {
            thread::sleep(Duration::from_secs(2));
            let _ = ensure_root(serial);
            return Ok(());
        }
        thread::sleep(Duration::from_secs(1));
    }
    Err("重启后等待 ADB 超时".to_string())
}

pub fn kill_tuya_process(serial: &str) {
    let _ = shell(serial, "killall your_chat_bot_QIO_1.0.1.bin 2>/dev/null || true");
    let _ = shell(serial, "killall your_chat_bot 2>/dev/null || true");
}

#[cfg(test)]
mod tests {
    use super::{parse_cpuid, parse_devices, valid_serial};

    #[test]
    fn parses_adb_device_rows() {
        let devices = parse_devices(
            "List of devices attached\nABCD1234 device product:tina model:AITVBOX_A133 transport_id:4\n",
        );
        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].serial, "ABCD1234");
        assert_eq!(devices[0].model.as_deref(), Some("AITVBOX_A133"));
        assert_eq!(devices[0].transport_id.as_deref(), Some("4"));
    }

    #[test]
    fn keeps_unauthorized_device_visible_but_unusable() {
        let devices = parse_devices("List of devices attached\nABCD unauthorized\n");
        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].state, "unauthorized");
    }

    #[test]
    fn validates_serial_before_process_use() {
        assert!(valid_serial("192.168.1.54:5555") == false);
        assert!(valid_serial("A133-UNIT_07"));
        assert!(!valid_serial("A133;reboot"));
    }

    #[test]
    fn parses_normalized_cpuid() {
        let cpuid = parse_cpuid("sunxi_serial : AABBCCDDEEFF00112233445566778899\n");
        assert_eq!(cpuid.as_deref(), Some("aabbccddeeff00112233445566778899"));
        assert!(parse_cpuid("sunxi_serial : 1234").is_none());
    }
}
