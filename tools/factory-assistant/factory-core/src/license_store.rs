//! Local Tuya license inventory (SQLite).

use crate::signing::sha256_hex;
use calamine::{open_workbook_auto, Data, Reader};
use rusqlite::{params, Connection, OptionalExtension};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct LicenseRow {
    pub id: i64,
    pub tuya_pid: String,
    pub uuid: String,
    pub auth_key: String,
    pub state: String,
    pub assigned_cpuid: Option<String>,
}

#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InventorySummary {
    pub available: u32,
    pub reserved: u32,
    pub written: u32,
    pub shipped: u32,
    pub total: u32,
}

#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportReport {
    pub imported: u32,
    pub skipped_duplicate: u32,
    pub failed: u32,
    pub errors: Vec<String>,
}

pub struct LicenseStore {
    path: PathBuf,
}

impl LicenseStore {
    pub fn open(data_dir: &Path) -> Result<Self, String> {
        std::fs::create_dir_all(data_dir)
            .map_err(|e| format!("无法创建数据目录 {}: {e}", data_dir.display()))?;
        // 规范文件名：始终在 FACTORY_DATA_DIR 下的 tuya_licenses.sqlite
        let path = data_dir.join("tuya_licenses.sqlite");
        let store = Self { path: path.clone() };
        store.with_conn(|conn| {
            conn.execute_batch(
                r#"
                CREATE TABLE IF NOT EXISTS tuya_licenses (
                  id INTEGER PRIMARY KEY,
                  tuya_pid TEXT NOT NULL,
                  uuid TEXT NOT NULL,
                  auth_key TEXT NOT NULL,
                  uuid_sha256 TEXT NOT NULL,
                  auth_key_sha256 TEXT NOT NULL,
                  state TEXT NOT NULL,
                  assigned_cpuid TEXT,
                  production_id TEXT,
                  imported_at TEXT NOT NULL,
                  assigned_at TEXT,
                  written_at TEXT,
                  note TEXT,
                  UNIQUE(tuya_pid, uuid)
                );
                CREATE INDEX IF NOT EXISTS idx_tuya_lic_state
                  ON tuya_licenses(tuya_pid, state);
                "#,
            )
            .map_err(|e| format!("初始化 License 库失败: {e}"))
        })?;
        Ok(store)
    }

    fn with_conn<T>(&self, f: impl FnOnce(&Connection) -> Result<T, String>) -> Result<T, String> {
        let conn = Connection::open(&self.path)
            .map_err(|e| format!("打开 License 库失败: {e}"))?;
        f(&conn)
    }

    pub fn summary(&self, tuya_pid: &str) -> Result<InventorySummary, String> {
        self.with_conn(|conn| {
            let mut available = 0u32;
            let mut reserved = 0u32;
            let mut written = 0u32;
            let mut shipped = 0u32;
            let mut total = 0u32;
            let mut stmt = conn
                .prepare("SELECT state, COUNT(*) FROM tuya_licenses WHERE tuya_pid=?1 GROUP BY state")
                .map_err(|e| e.to_string())?;
            let rows = stmt
                .query_map(params![tuya_pid], |row| {
                    Ok((row.get::<_, String>(0)?, row.get::<_, i64>(1)?))
                })
                .map_err(|e| e.to_string())?;
            for row in rows {
                let (state, count) = row.map_err(|e| e.to_string())?;
                let c = count as u32;
                total += c;
                match state.as_str() {
                    "AVAILABLE" => available = c,
                    "RESERVED" => reserved = c,
                    "WRITTEN" => written = c,
                    "SHIPPED" => shipped = c,
                    _ => {}
                }
            }
            Ok(InventorySummary {
                available,
                reserved,
                written,
                shipped,
                total,
            })
        })
    }

    pub fn import_pairs(
        &self,
        tuya_pid: &str,
        pairs: &[(String, String)],
    ) -> Result<ImportReport, String> {
        validate_pid(tuya_pid)?;
        let mut report = ImportReport {
            imported: 0,
            skipped_duplicate: 0,
            failed: 0,
            errors: Vec::new(),
        };
        self.with_conn(|conn| {
            let now = chrono_now();
            for (uuid, auth) in pairs {
                match validate_uuid_auth(uuid, auth) {
                    Ok(()) => {
                        let uuid_sha = sha256_hex(uuid.as_bytes());
                        let auth_sha = sha256_hex(auth.as_bytes());
                        let result = conn.execute(
                            r#"INSERT INTO tuya_licenses
                               (tuya_pid, uuid, auth_key, uuid_sha256, auth_key_sha256, state, imported_at)
                               VALUES (?1,?2,?3,?4,?5,'AVAILABLE',?6)"#,
                            params![tuya_pid, uuid, auth, uuid_sha, auth_sha, now],
                        );
                        match result {
                            Ok(_) => report.imported += 1,
                            Err(rusqlite::Error::SqliteFailure(err, _))
                                if err.code == rusqlite::ErrorCode::ConstraintViolation =>
                            {
                                report.skipped_duplicate += 1;
                            }
                            Err(e) => {
                                report.failed += 1;
                                report.errors.push(format!("uuid={uuid}: {e}"));
                            }
                        }
                    }
                    Err(e) => {
                        report.failed += 1;
                        report.errors.push(e);
                    }
                }
            }
            Ok(report)
        })
    }

    pub fn import_file(&self, tuya_pid: &str, path: &Path) -> Result<ImportReport, String> {
        let pairs = load_license_pairs(path)?;
        self.import_pairs(tuya_pid, &pairs)
    }

    /// Reserve a license for cpuid. Returns existing binding if any.
    pub fn reserve_for_cpuid(
        &self,
        tuya_pid: &str,
        cpuid: &str,
        production_id: &str,
    ) -> Result<LicenseRow, String> {
        validate_pid(tuya_pid)?;
        let cpuid = cpuid.trim().to_ascii_lowercase();
        self.with_conn(|conn| {
            // Existing permanent bind
            if let Some(row) = conn
                .query_row(
                    r#"SELECT id, tuya_pid, uuid, auth_key, state, assigned_cpuid
                       FROM tuya_licenses
                       WHERE tuya_pid=?1 AND assigned_cpuid=?2
                       LIMIT 1"#,
                    params![tuya_pid, cpuid],
                    map_license_row,
                )
                .optional()
                .map_err(|e| e.to_string())?
            {
                return Ok(row);
            }

            let now = chrono_now();
            let tx = conn.unchecked_transaction().map_err(|e| e.to_string())?;
            let id: i64 = tx
                .query_row(
                    r#"SELECT id FROM tuya_licenses
                       WHERE tuya_pid=?1 AND state='AVAILABLE'
                       ORDER BY id LIMIT 1"#,
                    params![tuya_pid],
                    |row| row.get(0),
                )
                .map_err(|_| {
                    "FAIL_LICENSE_EMPTY: 涂鸦 License 库存不足，请导入 xlsx/CSV".to_string()
                })?;
            tx.execute(
                r#"UPDATE tuya_licenses
                   SET state='RESERVED', assigned_cpuid=?1, production_id=?2, assigned_at=?3
                   WHERE id=?4"#,
                params![cpuid, production_id, now, id],
            )
            .map_err(|e| e.to_string())?;
            tx.commit().map_err(|e| e.to_string())?;

            conn.query_row(
                r#"SELECT id, tuya_pid, uuid, auth_key, state, assigned_cpuid
                   FROM tuya_licenses WHERE id=?1"#,
                params![id],
                map_license_row,
            )
            .map_err(|e| e.to_string())
        })
    }

    pub fn mark_written(&self, id: i64) -> Result<(), String> {
        self.with_conn(|conn| {
            conn.execute(
                "UPDATE tuya_licenses SET state='WRITTEN', written_at=?1 WHERE id=?2",
                params![chrono_now(), id],
            )
            .map_err(|e| e.to_string())?;
            Ok(())
        })
    }

    pub fn mark_shipped(&self, id: i64) -> Result<(), String> {
        self.with_conn(|conn| {
            conn.execute(
                "UPDATE tuya_licenses SET state='SHIPPED' WHERE id=?1",
                params![id],
            )
            .map_err(|e| e.to_string())?;
            Ok(())
        })
    }
}

fn map_license_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<LicenseRow> {
    Ok(LicenseRow {
        id: row.get(0)?,
        tuya_pid: row.get(1)?,
        uuid: row.get(2)?,
        auth_key: row.get(3)?,
        state: row.get(4)?,
        assigned_cpuid: row.get(5)?,
    })
}

fn chrono_now() -> String {
    chrono::Local::now().format("%Y-%m-%dT%H:%M:%S").to_string()
}

pub fn validate_pid(pid: &str) -> Result<(), String> {
    if pid.len() == 16 && pid.chars().all(|c| c.is_ascii_alphanumeric()) {
        Ok(())
    } else {
        Err("TUYA_PID 必须是 16 位字母或数字".to_string())
    }
}

pub fn validate_uuid_auth(uuid: &str, auth: &str) -> Result<(), String> {
    let uuid = uuid.trim();
    let auth = auth.trim();
    if uuid.chars().count() != 20 || uuid.chars().any(|c| c.is_whitespace()) {
        return Err(format!("UUID 长度/格式错误: len={}", uuid.chars().count()));
    }
    if auth.chars().count() != 32 || auth.chars().any(|c| c.is_whitespace()) {
        return Err(format!(
            "AuthKey 长度/格式错误: len={}",
            auth.chars().count()
        ));
    }
    Ok(())
}

pub fn license_file_bytes(uuid: &str, auth_key: &str) -> String {
    format!("TUYA_OPENSDK_UUID={uuid}\nTUYA_OPENSDK_AUTHKEY={auth_key}\n")
}

pub fn load_license_pairs(path: &Path) -> Result<Vec<(String, String)>, String> {
    let ext = path
        .extension()
        .and_then(|s| s.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    if matches!(ext.as_str(), "xlsx" | "xls" | "xlsm" | "xlsb" | "ods") {
        load_from_spreadsheet(path)
    } else if ext == "csv" || ext == "env" || ext == "txt" {
        load_from_text(path)
    } else {
        // try spreadsheet then text
        load_from_spreadsheet(path).or_else(|_| load_from_text(path))
    }
}

fn load_from_spreadsheet(path: &Path) -> Result<Vec<(String, String)>, String> {
    let mut workbook = open_workbook_auto(path)
        .map_err(|e| format!("无法打开表格 {}: {e}", path.display()))?;
    let sheet_names = workbook.sheet_names().to_vec();
    let sheet = sheet_names
        .first()
        .ok_or_else(|| "表格没有工作表".to_string())?;
    let range = workbook
        .worksheet_range(sheet)
        .map_err(|e| format!("读取工作表失败: {e}"))?;

    let mut rows = range.rows();
    let header = rows.next().ok_or_else(|| "表格为空".to_string())?;
    let headers: Vec<String> = header
        .iter()
        .map(|c| cell_string(c).to_ascii_lowercase())
        .collect();
    let uuid_col = find_col(&headers, &["uuid", "设备uuid", "deviceuuid"])?;
    let key_col = find_col(
        &headers,
        &["key", "authkey", "auth_key", "授权码", "auth key"],
    )?;

    let mut pairs = Vec::new();
    for row in rows {
        let uuid = row.get(uuid_col).map(cell_string).unwrap_or_default();
        let key = row.get(key_col).map(cell_string).unwrap_or_default();
        if uuid.trim().is_empty() && key.trim().is_empty() {
            continue;
        }
        pairs.push((uuid.trim().to_string(), key.trim().to_string()));
    }
    if pairs.is_empty() {
        return Err("表格中未解析到任何 License 行".to_string());
    }
    Ok(pairs)
}

fn load_from_text(path: &Path) -> Result<Vec<(String, String)>, String> {
    let text = std::fs::read_to_string(path)
        .map_err(|e| format!("读取文件失败 {}: {e}", path.display()))?;
    // license.env single pair
    if text.contains("TUYA_OPENSDK_UUID=") {
        let mut uuid = String::new();
        let mut auth = String::new();
        for line in text.lines() {
            let line = line.trim();
            if let Some(v) = line.strip_prefix("TUYA_OPENSDK_UUID=") {
                uuid = v.trim().to_string();
            }
            if let Some(v) = line.strip_prefix("TUYA_OPENSDK_AUTHKEY=") {
                auth = v.trim().to_string();
            }
        }
        return Ok(vec![(uuid, auth)]);
    }

    let mut rdr = csv::ReaderBuilder::new()
        .flexible(true)
        .from_reader(text.as_bytes());
    let headers = rdr
        .headers()
        .map_err(|e| e.to_string())?
        .iter()
        .map(|h| h.to_ascii_lowercase())
        .collect::<Vec<_>>();
    let uuid_col = find_col(&headers, &["uuid", "设备uuid"])?;
    let key_col = find_col(&headers, &["key", "authkey", "auth_key", "授权码"])?;
    let mut pairs = Vec::new();
    for rec in rdr.records() {
        let rec = rec.map_err(|e| e.to_string())?;
        let uuid = rec.get(uuid_col).unwrap_or("").trim().to_string();
        let key = rec.get(key_col).unwrap_or("").trim().to_string();
        if uuid.is_empty() && key.is_empty() {
            continue;
        }
        pairs.push((uuid, key));
    }
    Ok(pairs)
}

fn find_col(headers: &[String], aliases: &[&str]) -> Result<usize, String> {
    for (i, h) in headers.iter().enumerate() {
        let clean = h.replace([' ', '_', '-'], "");
        for a in aliases {
            let aa = a.replace([' ', '_', '-'], "");
            if clean == aa || h.contains(a) {
                return Ok(i);
            }
        }
    }
    Err(format!(
        "找不到列 {:?}, 实际表头: {:?}",
        aliases, headers
    ))
}

fn cell_string(cell: &Data) -> String {
    match cell {
        Data::Empty => String::new(),
        Data::String(s) => s.clone(),
        Data::Float(f) => {
            // avoid scientific notation for ids
            if f.fract() == 0.0 {
                format!("{}", *f as i64)
            } else {
                f.to_string()
            }
        }
        Data::Int(i) => i.to_string(),
        Data::Bool(b) => b.to_string(),
        Data::DateTime(dt) => dt.to_string(),
        Data::DateTimeIso(s) | Data::DurationIso(s) => s.clone(),
        Data::Error(e) => format!("{e:?}"),
    }
}
