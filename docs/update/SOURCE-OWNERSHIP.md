# 首轮 A133 源码归属与迁移边界

适用 MIMI-UPDATE-001；公开应用及统一发布入口为 `dshanpi/SmartDesktopMimi`，
私有 SDK overlay 为 `100askTeam/A133-AIDesktop-Tina5`。

## 当前可执行边界

| 范围 | 归属及更新方式 |
| --- | --- |
| LVGL 桌面、内置应用界面、字体、图标、Web 静态资源 | 应用源码；随签名桌面包发布 |
| lv_backend、service_*、Wi-Fi/蓝牙/传感器/LED HAL、TPlayer | 平台服务；仅系统发布可替换 |
| apps/ipkvm/video、IPKVM Go 服务、platform 下的系统服务 | 平台服务；Web 静态文件可独立更新，服务程序不可夹入应用包 |
| 更新器及 packaging 系统启动、身份、公钥配置 | 系统基础设施；源码可公开，二进制只能经系统发布 |
| 内核、设备树、Bootloader、厂商 SDK 修改与板级配置 | 私有 SDK 的 overlay；不进入公开源码包 |
| V1/V2 协议、共享配置/日志接口 | 双方依赖；协议锁与逐文件归属锁约束，不宣称 V2 全量迁移 |

当前 `source-ownership.json` 对混合目录逐文件登记，使用四种归属：

- `application`：应用拥有的源码。
- `platform`：平台服务源码，应用编译清单中出现即拒绝。
- `shared-frozen`：两侧使用的兼容实现和头文件，当前基线固定字节。
- `platform-client-frozen`：仍链接进桌面的硬件适配客户端，当前基线固定字节。

亮度、音量、PCM、Sunxi 显示和输入适配目前尚未完全移出桌面。这些过渡文件
固定 SHA-256；修改时必须评审接口和系统基线、重新验收，不能仅重写锁后沿用旧证据。
`migration_complete=false` 明确保留，不能把登记完成当成物理拆仓完成。

## 构建及发布检查

`tools/update/source_ownership.py` 核对敏感目录文件全集与冻结哈希；新增系统源码或
删除已登记文件会失败。应用编译使用显式 SYSTEM_SOURCES，禁用私有云 Provider。
编译结束再次检查真实 compile_commands.json，拒绝平台服务、未知来源、链接别名和
仓库外输入。构建回执记录归属锁哈希；打包与发布必须携带同一份
`source-ownership.json`，并与签名负载中的构建回执对应。

平台源码留在当前受控工作候选中，下一步按服务依赖迁出；迁出前应同时核对 include、
链接依赖、安装配方和固定接口兼容。系统锁同时记录公开配方提交与私有 SDK overlay
提交，不以其中任一提交代替另一方。SDK overlay 的应用入口保持
`scripts/apply_overlay.sh /path/to/TinaSDK`，不得复制整个 SDK 到公开应用仓库。

## 尚未完成

平台源码物理迁移、应用 SDK 内的平台客户端交付、真实设备对旧后端的兼容验收，
以及最终镜像、升级、断电和发布验证仍需继续。本文不为 T527/RK3576 提供支持证明。


## SPLIT053 独立SDK sourcecheckpoint（2026-10-15）

私有SDK `958c62f739c86fcc1128fb2d838faa25c550aaa9` 已增加
`overlay/openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-provider`，
内含private/public独立source profile、真实依赖闭包配方和原SHA构建器。
两profile均已独立编译且与各自旧参考字节一致；标准overlay95基准/4248文件
重放及私有repo独立远端读回通过。实际恢复SDK新增该组件并编译private版本通过。
厂商100ask实现仍外置，公开stub不读私有依赖。

这一步没有把源码登记当完成：系统build entry尚未切换到SDK组件，应用工作候选
仍有旧平台实现和冻结客户端，最终应用SDK/业务归属也未收尾。
`migration_complete=false` 继续保留。详细证据与失败历史见SPLIT053门禁。
冻结sysrc2来源锁继续指向构建当时实际1fb输入；958为新的sourcecheckpoint，
不得倒填旧镜像、复制其真机结论或提前上传应用/Release。
