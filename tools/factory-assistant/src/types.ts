export type RunPhase =
  | "idle"
  | "scanning"
  | "readingIdentity"
  | "preparing"
  | "writing"
  | "verifying"
  | "validating"
  | "hardwareTest"
  | "credentialPassed"
  | "passed"
  | "failed";

export interface DeviceSummary {
  serial: string;
  state: string;
  model?: string;
  product?: string;
  transportId?: string;
}

export interface DeviceIdentity {
  serial: string;
  cpuid?: string;
  firmwareVersion?: string;
  tuyaPid?: string;
}

export interface ConfigCheck {
  name: string;
  configured: boolean;
  sensitive: boolean;
}

export interface EventRecord {
  timestamp: string;
  level: "info" | "success" | "warning" | "error";
  message: string;
}

export type HardwareTestKind = "automatic" | "interactive";
export type HardwareTestStatus =
  | "waiting"
  | "running"
  | "passed"
  | "failed"
  | "needsConfirmation";
export type HardwareStatus = "idle" | "running" | "needsConfirmation" | "passed" | "failed";

export interface HardwareTestResult {
  id: string;
  label: string;
  group: string;
  kind: HardwareTestKind;
  status: HardwareTestStatus;
  summary: string;
  value?: string;
  detail?: string;
  command?: string;
  rawOutput?: string;
  durationMs?: number;
}

export interface LicenseInventoryView {
  available: number;
  reserved: number;
  written: number;
  shipped: number;
  total: number;
}

export interface StationSnapshot {
  hostPlatform: string;
  stationName: string;
  batchNumber: string;
  adbAvailable: boolean;
  cloudConfigured: boolean;
  busy: boolean;
  phase: RunPhase;
  progress: number;
  statusMessage: string;
  devices: DeviceSummary[];
  selectedDevice?: DeviceIdentity;
  configChecks: ConfigCheck[];
  hardwareStatus: HardwareStatus;
  hardwareProgress: number;
  hardwareTests: HardwareTestResult[];
  events: EventRecord[];
  licenseInventory?: LicenseInventoryView;
  tuyaPid?: string;
  factoryHome?: string;
  licenseDbPath?: string;
}
