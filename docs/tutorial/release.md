# 产品发布与验收资料

本章供维护者使用。发布文档站与发布产品固件是两条独立流程：站点可以在普通 Ubuntu 和 GitHub 托管 runner 构建，产品发布需要受控输入、签名和目标验收。

## 版本命名

- 应用标签：`vX.Y.Z`，例如测试版本包含合法 `-rc.N`。
- 系统标签：`system-a133-vX.Y.Z`。
- 对外系统镜像：`SmartDesktopMimi-SystemImage-v<系统版本>.img.gz`。
- 应用与系统在同一公共仓库 Releases 发布，但版本独立。

旧标签不移动、同版本资产不覆盖。修复需要新版本和对应的新证据。

## 应用候选资料

至少包含应用载荷、`manifest.json` 与签名、`application-build.json`、SDK/依赖锁、平台契约锁、源码归属、源码归档、SBOM、目标描述，以及实际验收资料。`release-lock.json` 必须与真实包和来源一致。

## 系统候选资料

包含完整镜像、系统清单与签名、`system-build.json`、公开 SDK 组件清单、依赖锁、出厂应用清单/签名、源码归档、SBOM 和目标验收；声明 OTA 时增加 `system.swu`。

公开资料只放获准公开的来源标识与摘要，私有 SDK 补丁、受限输入、内部归档路径和设备秘密不作为公共附件。

## 标准流程

1. 固定源码和 SDK 输入，执行实际构建。
2. 核对最终文件、启动链、版本与产品组成。
3. 用已有产品信任体系签名；不在每次发布时换新密钥。
4. 运行实际产物的主机与实机验收，绑定包哈希和目标。
5. 组装候选，检查每份来源记录和资产哈希。
6. 通过当前上传准入后创建受控发布，再下载远端资产复验。
7. 只有符合对应发布状态的候选才推广，不用备注代替失败门禁。

组装工具的实际接口：

```sh
python3 tools/update/prepare_release.py \
  --inputs /secure/reviewed-plan.json \
  --output /absolute/path/to/new-candidate \
  --checker /absolute/path/to/mimi-release-checker \
  --trust /secure/existing-product-trust.json
```

这是一条需要既有审核输入的维护命令，不是从零生成“通过证明”的示例。缺少验收报告时工具可生成全 `not_run` 的模板，该模板不具备上传资格。

## Draft、Pre-release、Stable

| 状态 | 说明 |
| --- | --- |
| Draft | 受控候选；访问需权限；不参与设备自动更新 |
| Pre-release | 满足当前规则的测试候选，仅显式测试通道选择 |
| Stable | 所有宣称目标和必需更新路径均已通过，且无未解决阻断 |

历史曾有单独授权的受控草稿同步。那次授权与附件验证只对当批成立，不能理解为以后所有未验收包都可以绕过上传检查。

## 本快照为什么没发布 rc33/rc10

11 月 2 日记录了三类问题：产品发布专用 runner/环境未配置；rc33 的完整验收仍缺项；rc10 构建清单的应用版本与签名出厂版本不一致。流程在上传前停止，没有新建这些标签/Release。

解决顺序应为修正正常构建来源、完成实际验收、准备受控发布设施，再执行既有入口；不能把版本字段手改一致就放行旧产物。

详细规则见[发布手册](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/update/RELEASE-RUNBOOK.md)、[自动发布实际结果](https://github.com/dshanpi/SmartDesktopMimi/blob/75500c76e389971bfb3bd0a45ac4bed74e1f8cfa/docs/validation/quality076-auto-release-20261102.json)。
