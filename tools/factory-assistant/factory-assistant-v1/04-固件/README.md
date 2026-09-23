# 固件

完整 eMMC 镜像体积大，**未复制进本交接包**。

## 仓库内路径

```text
build/a133_linux_b6_uart0.img
```

同目录也可能有历史变体：`build/firmware/` 下带后缀的 img。  
量产/联调请与产品确认当前冻结版本。

## 本目录文件

| 文件 | 说明 |
|---|---|
| `aitvbox-version.txt` | 打包时从 `build/aitvbox-version` 复制的版本信息 |
| `a133_linux_b6_uart0.img.sha256` | 镜像 SHA256（若生成时镜像存在） |
| `image-size.txt` | 镜像大小提示 |

## 烧录

- 工具：全志 PhoenixSuit / LiveSuit  
- **所有设备烧同一份 img**  
- 烧完启动后，凭据由出厂助手写入，不要改镜像  

## 设备上版本/PID

```text
cat /etc/aitvbox-version
cat /etc/aitvbox-tuya-pid
```
