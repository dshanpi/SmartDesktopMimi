# 准备产品发布资料并复核交付

本章面向维护者，将已构建和已验证的产物整理为可追溯交付。产品发布需要目标验收、受控签名与上传准入；文档站部署使用[独立流程](site.md)。

## 确定版本与发布类型

| 项目 | 命名与规则 |
| --- | --- |
| 应用标签 | `vX.Y.Z`，预发布可包含 `-rc.N` |
| 系统标签 | `system-a133-vX.Y.Z` |
| 对外系统镜像 | `SmartDesktopMimi-SystemImage-v<系统版本>.img.gz` |
| 发布位置 | 应用与系统统一在本仓库 Releases，版本独立 |

已有标签和同版本资产保持不变。修订产物使用新版本，并重新绑定验收结果。

## 汇总本次实际产物

先建立输入表，逐项填写真实来源与文件位置：

| 应用候选 | 系统候选 |
| --- | --- |
| 应用载荷、manifest 和签名 | 完整镜像、系统清单和签名 |
| `application-build.json` | `system-build.json` |
| SDK、依赖与平台契约锁 | SDK 组件、依赖及硬件来源锁 |
| 源码归属、源码归档、SBOM | 源码归档、SBOM、分区与启动输入 |
| 完整目标与验收记录 | 出厂应用清单/签名、目标验收；声明 OTA 时附 SWU |

检查 `release-lock.json`、包摘要、版本、目标和验收报告互相一致。私有来源只公开允许分发的标识与摘要，不将 SDK 私有补丁、设备秘密或内部归档作为公共附件。

## 绑定真实验收结果

1. 固定本次源码提交、包 SHA-256 与目标设备。
2. 收集对应主机构建、兼容拒绝和设备功能结果。
3. 完成声明范围内的升级、回退、断电与数据保留。
4. 将未执行、失败和缺条件项分别记录。
5. 对照当前发布状态的要求判断是否具备上传或推广条件。

验收必须对应实际包。重新构建导致包变化时，重跑受影响检查并更新关联。

## 组装受控发布候选

在 **Ubuntu 发布主机、产品源码根目录**使用已准备的审核计划、检查器和既有信任输入。以下路径全部替换为实际值：

```sh
python3 tools/update/prepare_release.py \
  --inputs /secure/reviewed-plan.json \
  --output /absolute/path/to/new-candidate \
  --checker /absolute/path/to/mimi-release-checker \
  --trust /secure/existing-product-trust.json
```

**检查结果：** 工具成功完成组装，生成记录与预期版本、来源和摘要一致。工具允许生成全 `not_run` 的验收模板时，该模板仅用于补齐检查，不构成上传资格。

缺少计划 schema、检查器或信任材料时，按项目发布手册准备，不自行写入“通过”字段替代实际校验。

## 选择并核对发布状态

| 状态 | 适用范围 | 推进条件 |
| --- | --- | --- |
| Draft | 受控候选，需访问权限 | 满足当前批次上传准入，保留真实未完项 |
| Pre-release | 显式测试通道 | 满足测试发布规则，无被禁止的已知失败 |
| Stable | 普通使用者与稳定通道 | 所有宣称目标及必需路径通过，无未解决阻断 |

历史某批受控草稿同步授权只适用于该批材料。后续发布按当前计划与门禁处理。

## 上传后重新下载验证

1. 核对远端标签、源码提交和发布状态。
2. 将本次远端附件下载到独立复核目录。
3. 按本次签名与校验清单验证完整性和目标。
4. 核对所有必要附件齐全，没有旧版本文件混入。
5. 保存远端地址、实际摘要和检查结果，再按条件推广。

上传成功只说明文件已传输，完成交付还需要远端读回复核。

## 处理当前快照中的阻断

固定快照的 rc33/rc10 缺少完整实机验收、产品专用 runner/环境，并存在 rc10 来源记录中 rc32 与签名出厂 rc33 不一致。对应流程已在上传前停止。

维护顺序为：修正构建来源 → 产生一致候选 → 完成实际验收 → 配置发布设施 → 执行准入与远端复核。历史详情见[版本状态](version-status.md)。

## 查阅依据

[发布执行手册](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/RELEASE-RUNBOOK.md) · [组装工具](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/tools/update/prepare_release.py) · [发布阻断记录](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-auto-release-20261102.json)
