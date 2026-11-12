# 版本与已知限制

版本号、构建成功、远端可下载和实机可用是不同结论。本章把历史记录放在同一张表里，便于选择资料和定位设备。

## 教程基线

| 项目 | 固定值 / 来源 |
| --- | --- |
| 文档整理日期 | 2026-11-12 |
| 产品说明参考 | `checkpoint/takeover-20261022` |
| 固定源码提交 | `75500c76e389971bfb3bd0a45ac4bed74e1f8cfa` |
| 此前 main 快照 | `38e3b502dd17213a67431bf5fa4c6c096637010e`，2026-10-15 PAIR071 记录 |
| 已登记的参考 PCB | A133 MainBoard V1.2；B6 是 SDK 目标名 |
| 最新开发状态来源 | [CURRENT_PROGRESS](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/CURRENT_PROGRESS.md) |

这里的“最新”指上述固定快照内的记录。日后新发布应以对应 Release 清单、签名和验收附件为准。

## 已发生的关键事件

| 时间 | 记录事实 | 如何理解 |
| --- | --- | --- |
| 2026-10-15 | rc26 应用及系统 rc5/rc6 受控草稿附件远端复验 | 草稿下载验证，不是稳定发布或实机升级合格 |
| 2026-10-16 | rc27/rc28 组件布局候选及主机验证 | 新页面和模拟输入证据；不能代替实体触摸 |
| 2026-10-28 | rc33 应用、rc10 系统构建与签名候选 | 新候选有构建证据，未完成装板验收 |
| 2026-10-29 | rc33/rc10 安装未成功；设备恢复旧系统 rc8 / 应用 rc31 | 旧系统恢复不计作新候选通过 |
| 2026-11-02 | 自动发布流程在验收门禁前停止 | 未创建对应新标签或 Release，未上传该批附件 |

11 月 2 日还记录了 rc10 构建清单中的应用版本 rc32 与签名出厂应用 rc33 不一致。应通过正确的构建与来源记录解决，不能编辑旧清单后宣称原产物已合格。

## 当前尚未闭合的事项

- SDIO / CMD52 超时、历史黑屏或卡住、非人工重启和云端重连阻塞的完整根因与稳定性验证。
- 新候选真实安装、冷启动、触摸导航、音频、Wi-Fi、HDMI/HID、长期运行。
- 应用与系统的双向升级/回退、关键阶段断电、用户数据保留。
- 私有云绑定、涂鸦项目与真实设备控制的目标环境验收。
- 产品自动发布所需的专用 runner、环境和完整候选资料。

:::note[文档构建环境与产品构建环境]
普通 Ubuntu 主机可以编写和构建本教程；它不因文档构建成功而获得 Tina SDK、设备授权或签名生产环境。本文档部署使用 GitHub 托管 runner，与产品发布 runner 是两条流程。
:::

## 阅读验证结果的方法

| 标记 | 含义 |
| --- | --- |
| `passed` | 指定输入和范围的检查通过 |
| `failed` | 已执行且发现问题 |
| `not_run` | 没有执行，不能推断通过 |
| `blocked` | 缺少必要条件或被门禁拒绝 |
| `deferred` | 已明确后置，仍待完成 |

确认一条 passed 时，同时查看：源码 SHA、包 SHA-256、设备板型、系统基线、检查方法。主机截图只证明该次渲染；进程存在只证明进程存在；网页 HTTP 200 只证明页面返回成功。

证据：[安装失败与恢复](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-board-install-20261029.json) · [自动发布阻断](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-auto-release-20261102.json) · [历史受控交付](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/delivery/PAIR071-SNAPSHOTS-20261015.md)。
