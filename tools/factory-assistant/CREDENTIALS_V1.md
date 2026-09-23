# 凭据写入 V1

## 你日常只做两件事

1. **采购后**：把涂鸦 xlsx 放进  
   `tools/factory-assistant/station/imports/`  
2. **产线**：连一台设备 → 导入（若刚放了新表）→ 写号  

```bash
cd tools/factory-assistant
cargo build --manifest-path factory-core/Cargo.toml -q
CLI=factory-core/target/debug/factory_cli

# 把 xlsx 放进 station/imports/ 后：
$CLI import              # 自动取 imports 里最新 xlsx
$CLI provision           # 自动用当前唯一 ADB 设备
```

**不用**每次：

- `cp` 私钥  
- `export FACTORY_HOME`  
- `export TUYA_PID`（默认已是 `alon7qgyjj8yus74`）  
- 手写 serial（仅一台设备时）

---

## 固定项（程序内置 / 自动）

| 项 | 约定 |
|---|---|
| 数据目录 | 工程包 `station/` |
| 库存库 | `station/data/tuya_licenses.sqlite` |
| 私钥 | `station/keys/100ask_ecdsa.pem`（由受控工程包的英文 `keys/` 目录提供，或仅在内部初始化时显式设置 `AITVBOX_FACTORY_KEY_DIR` / `AITVBOX_100ASK_SDK_ROOT`） |
| 涂鸦 PID | 默认 `alon7qgyjj8yus74` |
| 写号校验 | ADB 读回，**不依赖 Wi-Fi** |
| 等云端 secret | 默认关（产线无网） |

唯一**不固定**的是：涂鸦采购的 xlsx（每批号不同）→ 放入 `imports/`。

---

## 正式 Windows 包

安装目录带 `station/`，私钥在发布时放进 `station/keys/`（一次性）。  
工人：导入 xlsx + 点生产。
