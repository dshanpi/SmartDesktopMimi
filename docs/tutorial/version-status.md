# 核对版本与教程适用范围

本章集中记录教程依据和已知限制，供选择资料、安装文件与验证结论时查询。正文操作以固定源码为依据，实际设备以对应交付版本为准。

## 核对参考基线

| 项目 | 值 |
| --- | --- |
| 文档核对日期 | 2026-11-12 |
| 产品参考分支 | `checkpoint/takeover-20261022` |
| 固定提交 | `75500c76e389971bfb3bd0a45ac4bed74e1f8cfa` |
| 文档维护分支 | `main` |
| 已登记参考硬件 | A133 MainBoard V1.2；B6 为 SDK 构建目标 |
| 旧 main 产品快照 | `38e3b502dd17213a67431bf5fa4c6c096637010e` |

文档提交未将开发分支的产品改动合并到 main。按[开发环境](environment.md)获取产品参考快照，按[文档维护](site.md)获取本站源码。

## 对照关键版本记录

| 日期 | 实际记录 | 使用时的边界 |
| --- | --- | --- |
| 2026-10-15 | rc26 应用、系统 rc5/rc6 的受控草稿附件远端复核 | 证明该批附件复核，未构成稳定版与实机升级合格 |
| 2026-10-16 | rc27/rc28 组件布局候选与主机检查 | 页面和模拟输入范围 |
| 2026-10-28 | rc33 应用与 rc10 系统构建、签名候选 | 该批构建产物存在，装板验收待完成 |
| 2026-10-29 | 新候选安装未成功，恢复旧应用 rc31 / 系统 rc8 | 旧版本恢复成功，不计入新候选安装 |
| 2026-11-02 | 自动发布在上传前停止 | 未创建该批新标签/Release 或上传附件 |

rc10 构建记录中的出厂应用 rc32 与签名出厂 rc33 存在不一致，需要从正确构建来源修复并重新生成一致候选。

## 确认未完成的产品项目

固定快照仍保留：

- SDIO/CMD52 超时、黑屏/卡住、非人工重启与重连阻塞的完整定位及稳定性复验。
- 新候选安装、冷启动、真实触摸、网络、音频、HDMI/HID 等设备检查。
- 应用与系统双向升级/回退、断电恢复和数据保留。
- 相应私有云绑定、真实涂鸦项目与设备控制验收。
- 产品发布专用 runner、环境及完整候选资料。

选择实际设备镜像时查对应交付说明，不能仅按较大的版本号选择工程候选。

## 阅读证据时核对五项信息

1. 证据对应的完整源码提交。
2. 实际安装或测试包的 SHA-256。
3. 板型、硬件修订与系统基线。
4. 检查方法是主机、模拟输入还是实体设备。
5. passed/failed/not_run 的具体范围和未完成项。

截图证明相应界面状态，HTTP 200 证明资源可访问，签名检查证明对应信任校验；这些结果按各自范围使用。

本教程站使用普通 Ubuntu 与 GitHub Pages 构建发布。产品 SDK、设备操作与稳定性验收另有输入和流程。

## 查阅依据

[固定产品进度](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/CURRENT_PROGRESS.md) · [安装与恢复](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-board-install-20261029.json) · [发布阻断](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-auto-release-20261102.json) · [历史草稿资料](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/delivery/PAIR071-SNAPSHOTS-20261015.md)
