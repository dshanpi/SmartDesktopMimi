# 公私仓发布边界

公开版本必须由审计后的快照新建仓库，禁止把当前含敏感历史的开发仓库直接推送或改写后宣称干净。

公共仓包含产品源码、契约、测试、构建工具、可再分发集成代码、文档、许可证和 SBOM。私有 `AI-DeskTopBox-ops` 保存工厂数据库、一机一密、签名私钥、发布凭据、客户配置、受限 SDK 及量产注入流程。

导出前先轮换历史生产信任材料，分类所有二进制/vendor 输入，排除生成产物，扫描文件内容和 Git 对象，复核第三方许可证并人工审查候选清单。只从候选快照初始化新历史；再在空目录 clone，重新执行 doctor、测试和可复现构建后发布。

门禁命令为：

```bash
python3 tools/public_release_audit.py
python3 tools/aitvbox.py test --platform a133 --scope public
python3 tools/export_public_source.py 目标目录 \
  --acknowledge-rotated-secrets \
  --acknowledge-licenses-reviewed
```

导出器要求源树已提交且干净。它会创建只含一个初始提交的新仓库，将已复核的
LVGL 精确版本实体化为普通源码，不保留开发仓的私有 submodule 地址；同时生成
按 `docs/public-release.json` 执行 A133 公开裁剪，并生成 SPDX/CycloneDX SBOM、
`release/SHA256SUMS`、可复现 `.tar.gz` 及校验文件。`release/excluded-content.json`
记录每个被排除的路径和字节数，`release/source-size.json` 记录候选树体积。
导出过程不会添加远程或推送。

审计覆盖 Git 索引和所有未被忽略的新文件，因此新加文件不会绕过门禁。以下内容可以为
内部构建保留在本机，但必须保持未跟踪并由根 `.gitignore` 排除：TuyaOpen 下载工具链和
构建目录、A133 受限 vendor 库、外部 100ask SDK 与私钥、工厂 License/数据库及现场验证产物。
不要用 `git add -f` 把这些路径重新加入公开候选。

可公开的文档 PDF 和图片必须在 `docs/public-assets.json` 中记录来源、
GPL-3.0-or-later 许可证与 SHA-256。原始帧缓冲、`output/` 下的浏览器截图、
固件镜像、归档和未分类大文件都会被阻断。审计输出只显示路径和规则编号，
不回显匹配到的凭据内容。

第三方目录名不是放行条件。每个保留的二进制素材都必须在
`docs/public-release.json` 中逐文件记录来源、SPDX 许可证、许可证文件和 SHA-256。
A133 profile 排除 netboot ISO、旧 IPKVM ARM 可执行文件、无再分发依据的 CircularXX
Web 字体、非 A133 预编译音频库、可选 Tuya 应用/显示引擎及上游 demo/test 语料。
导出后执行 `python3 tools/public_release_audit.py --candidate`；
任何声明排除但仍出现在候选仓中的文件都会阻断发布。

公开构建使用不访问网络的云服务 stub。内部构建必须同时显式设置
`AITVBOX_ENABLE_100ASK_CLOUD=ON` 和绝对路径 `AITVBOX_100ASK_SDK_ROOT`；禁止根据本地目录
自动启用私有 provider。

零九智造自有代码按 GPL-3.0-or-later 或 09make.inc 商业许可双授权，CLA 授予重新许可权。第三方许可不变，IPKVM 保持 GPL-2.0 独立软件包和进程。
