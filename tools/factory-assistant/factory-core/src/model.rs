use serde::Serialize;

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceSummary {
    pub serial: String,
    pub state: String,
    pub model: Option<String>,
    pub product: Option<String>,
    pub transport_id: Option<String>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceIdentity {
    pub serial: String,
    pub cpuid: Option<String>,
    pub firmware_version: Option<String>,
    pub tuya_pid: Option<String>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ConfigCheck {
    pub name: String,
    pub configured: bool,
    pub sensitive: bool,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EventRecord {
    pub timestamp: String,
    pub level: EventLevel,
    pub message: String,
}

#[derive(Clone, Copy, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum EventLevel {
    Info,
    Success,
    Warning,
    Error,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum RunPhase {
    Idle,
    Scanning,
    ReadingIdentity,
    Preparing,
    Writing,
    Verifying,
    Validating,
    HardwareTest,
    CredentialPassed,
    Passed,
    Failed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum HardwareTestKind {
    Automatic,
    Interactive,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum HardwareTestStatus {
    Waiting,
    Running,
    Passed,
    Failed,
    NeedsConfirmation,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum HardwareStatus {
    Idle,
    Running,
    NeedsConfirmation,
    Passed,
    Failed,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HardwareTestResult {
    pub id: String,
    pub label: String,
    pub group: String,
    pub kind: HardwareTestKind,
    pub status: HardwareTestStatus,
    pub summary: String,
    pub value: Option<String>,
    pub detail: Option<String>,
    pub command: Option<String>,
    pub raw_output: Option<String>,
    pub duration_ms: Option<u64>,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LicenseInventoryView {
    pub available: u32,
    pub reserved: u32,
    pub written: u32,
    pub shipped: u32,
    pub total: u32,
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StationSnapshot {
    pub host_platform: String,
    pub station_name: String,
    pub batch_number: String,
    pub adb_available: bool,
    pub cloud_configured: bool,
    pub busy: bool,
    pub phase: RunPhase,
    pub progress: u8,
    pub status_message: String,
    pub devices: Vec<DeviceSummary>,
    pub selected_device: Option<DeviceIdentity>,
    pub config_checks: Vec<ConfigCheck>,
    pub hardware_status: HardwareStatus,
    pub hardware_progress: u8,
    pub hardware_tests: Vec<HardwareTestResult>,
    pub events: Vec<EventRecord>,
    pub license_inventory: Option<LicenseInventoryView>,
    pub tuya_pid: String,
    /// 工位根目录（keys/data/imports/logs）
    pub factory_home: String,
    /// SQLite 库存路径
    pub license_db_path: String,
}
