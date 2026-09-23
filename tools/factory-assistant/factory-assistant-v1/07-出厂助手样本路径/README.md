# 现有出厂助手工程（仓库内，未整包复制）

样本与硬件测试已在产品仓库，请直接打开：

```text
tools/factory-assistant/
├── README.md                 # 工程说明、Windows 构建
├── factory-core/             # Rust 核心：adb / hardware / model
│   └── src/
│       ├── adb.rs            # 已有 CPUID/版本/PID 读取
│       ├── hardware.rs       # 硬件测试
│       └── model.rs          # RunPhase 含 preparing/writing/credentialPassed…
├── src/                      # React UI（硬件测试界面）
└── src-tauri/                # Tauri；Windows 正式版同源思路
```

## 已实现

- ADB 扫描、身份读取  
- 全套硬件自动项 + 人工确认项  
- 快照事件、工位 UI  

## 待按 01-规格补齐

- 本地签名模块  
- 涂鸦 License SQLite 库存  
- 写 `device_sig` / `license.env`  
- 等 secret / 涂鸦就绪  
- 生产台账  
- 与硬件测试组成最终 PASS  

## 注意

- 样本里仍有旧「量产云 mTLS」配置检查项 → V1 改为本地 `SIGNING_*` / `TUYA_PID` / `FACTORY_DATA_DIR`  
- Windows **不要**依赖 Bash 调 `factory_provision_device.sh`；逻辑进 `factory-core`  
