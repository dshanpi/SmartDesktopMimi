# 设备侧文档（拷贝）

| 文件 | 阅读重点 |
|---|---|
| `cloud.md` | 100ask provision、secret、MQTT、云端 OTA、凭据路径 |
| `tuyaopen.md` | 涂鸦一机一密、`license.env`、产测不绑用户 |
| `ota.md` | SWUpdate A/B；助手不执行 OTA，但需知凭据跨 OTA 保留 |

## 和出厂助手的关系

```text
助手写 device_sig + license.env
    → 设备自己 provision / 连涂鸦
    → 以后云端 OTA（与助手无关）
```

用户设置页 bindToken / App 扫码 **不是** 产测 PASS 条件。
