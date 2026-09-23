# TuyaOpen 补丁归档（已固化，非现役）

这些补丁**已永久 apply 进 `third_party/TuyaOpen/`**（vendored 源码），构建时不再打补丁。
留在此处仅作历史与 rebase 参考。

## 文件

| 文件 | 作用 | 原 apply 位置 |
|---|---|---|
| `0001-current-a133-tuyaopen-integration.patch` | TuyaOpen 主集成（chatbot、ai 组件、bridge v1 等） | 源码根 |
| `0002-main-ui-free-chat-command-bridge.patch` | 主界面 free-chat 命令桥（ai_mode_free、tuya_ai_agent、cmd 回复） | 源码根 |
| `0001-a133-b6-linux-platform-support.patch` | platform/LINUX A133_B6 平台支持 | `platform/LINUX` |
| `0002-persist-tuya-kv-on-rootfs_data.patch` | Tuya KV 持久化到 rootfs_data | `platform/LINUX` |
| `0003-runtime-device-license.patch` | UUID/AuthKey 一机一密运行时加载（chatbot + Linux adapter） | 源码根 |
| `0002-*.patch.bak-prerefresh-20260709` | 0002 刷新前的原始版（含与 0001 重复的 bridge，已废弃） | — |
| `0002-*.patch.bak.1783570190` | 0002 早期损坏版 | — |

## 为什么固化

原先构建时用 `patch -p1` 把这些补丁打到干净 vendored 源上。问题：改 TuyaOpen 侧代码
只能改补丁文件（手改 diff），上下文一漂移就 apply 失败——每次改源码编译都报错。

固化后：`third_party/TuyaOpen/` 本身就是已含全部定制的源（git 跟踪），改 TuyaOpen 代码
= 直接改 `third_party/TuyaOpen/` 里的文件，跟改普通代码一样，`git diff/commit` 直接可用。
`scripts/prepare_tuyaopen_build_root.sh` 已移除打补丁步骤。

## 升级 TuyaOpen 上游时怎么 rebase

1. 跑 `scripts/vendor_tuyaopen.sh` 把新上游 vendored 进 `third_party/TuyaOpen/`
   （⚠️ 会冲掉已固化的定制——定制是 git 跟踪的，`git diff` 能看到全部丢失，可恢复）。
2. 把本目录的 5 个 `.patch`（非 `.bak`）按原顺序重新 apply 到新 vendored 源：
   ```sh
   cd third_party/TuyaOpen
   patch -p1 < ../../integrations/tuyaopen/patches-archive/0001-current-a133-tuyaopen-integration.patch
   patch -p1 < ../../integrations/tuyaopen/patches-archive/0002-main-ui-free-chat-command-bridge.patch
   cd platform/LINUX
   patch -p1 < ../../../integrations/tuyaopen/patches-archive/0001-a133-b6-linux-platform-support.patch
   patch -p1 < ../../../integrations/tuyaopen/patches-archive/0002-persist-tuya-kv-on-rootfs_data.patch
   cd ../..
   patch -p1 < ../../integrations/tuyaopen/patches-archive/0003-runtime-device-license.patch
   ```
   冲突则手动解决（这是升级的固有成本；平时改代码不经过这里）。
3. 重新 `git add -f third_party/TuyaOpen` 提交。
