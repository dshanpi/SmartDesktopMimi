# 参考脚本（金标准行为）

这些脚本是 **Linux + adb** 下已验证的写机流程。  
Windows 出厂助手应在 Rust 中实现**同等步骤与校验**，不要依赖 Bash。

| 文件 | 作用 |
|---|---|
| `sign_device.sh` | 100ask ECDSA 签名 |
| `provision_device.sh` | 读 CPUID → 签名 → 写 `/etc/100ask/device_sig` |
| `provision_tuya_license.sh` | 写 `/factory/tuya/license.env` 并校验 |
| `lib.sh` | 脚本公共函数 |

## 在仓库环境中运行（推荐）

在 **产品仓库根目录**、设备已 adb 连接时：

```bash
# 签名并写 device_sig（私有 SDK 不随公开仓库分发）
AITVBOX_100ASK_SDK_ROOT=/absolute/private/100ask-iot-sdk \
  ./scripts/provision_device.sh

# 写涂鸦（准备好仅两行的 license.env）
./scripts/provision_tuya_license.sh /path/to/license.env
```

本目录是拷贝件；`provision_device.sh` 同样要求显式的私有 SDK 绝对路径。
若只使用本目录的 `sign_device.sh`，请设置：

```bash
export KEY_DIR="$(pwd)/../02-密钥"
# sign_device.sh 使用 KEY_DIR/100ask_ecdsa.pem
```

更稳妥：在完整产品仓中使用 `scripts/provision_device.sh`，并由受控存储提供 SDK 和密钥。

## 必须保留的行为

1. `adb root` + 读两次 CPUID 一致  
2. 等待 `/etc/100ask`、`/factory/tuya` 持久化挂载  
3. 同目录临时文件 → chmod 600 → mv 原子替换 → sync  
4. 读回校验；License 用内容 SHA256  
5. AuthKey **不要**出现在进程命令行参数与日志  
6. 写 License 后可 `killall` 涂鸦二进制以触发重载  

详见上级 `01-规格` 第 7 节。
