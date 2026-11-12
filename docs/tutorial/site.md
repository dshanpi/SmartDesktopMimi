# 维护并发布 GitHub Pages 教程

本站使用 Astro/Starlight，参考 EinkOS 的文档站结构，提供中文搜索、章节导航、代码复制、主题切换与 Mermaid。正文源文件在仓库 `main` 分支维护。

## 准备文档环境

在 **Ubuntu 文档主机**准备 Git、Node.js ≥22.12 和 npm。CI 使用 Node 24。使用新目录克隆包含本站的 `main`：

```sh
git clone --branch main https://github.com/dshanpi/SmartDesktopMimi.git MimiDocs
cd MimiDocs/docs
node --version
npm ci
npm run build
```

**检查结果：** 依赖安装、正文生成、测试、Astro 构建与链接/资源检查成功，输出位于 `docs/dist/`。首次安装后，依赖未变化时直接构建即可。

## 找到需要编辑的文件

```text
docs/tutorial/               正文，唯一人工编辑来源
docs/routes.mjs              页面路由、顺序、分组和图片来源
docs/source-links.mjs        相对链接到站点路由的转换
docs/astro.config.mjs        站点地址、base 与 Starlight 配置
docs/src/styles/custom.css   样式
docs/scripts/                内容准备、链接及构建检查
docs/package-lock.json       文档依赖锁
.github/workflows/docs.yml   构建与 Pages 部署
```

`src/content/docs/`、`dist/` 和生成的 vendor/图片为构建结果，不手工维护。历史工程资料保留原位置，通过固定提交链接引用。

## 按操作指南规范改写正文

1. 首段交代对象、环境与任务，说明影响操作的必要前提。
2. 用动作标题按准备、操作、检查、异常处理排序。
3. 命令前说明执行位置、目录和需要替换的输入。
4. 关键操作后给出可观察判据，例如状态、退出码、文件或版本。
5. 将就近失败处理放在对应步骤后，共用排障集中链接。
6. 历史版本与验证边界放到相应参考章节，截图注明来源。

这些规则依据本轮提供的 `write-professional-guides` 写作规范。具体技术事实仍以项目实现和实际证据为准。

## 添加或调整章节

1. 在 `docs/tutorial/` 创建 Markdown，首行为 `# 标题`。
2. 在 `routes.mjs` 登记 `source`、`slug`、`title`、`group`。
3. 同目录教程使用 `chapter.md` 相对链接，产品源码使用固定 SHA 链接。
4. 新图片登记公开来源并保留相对引用。
5. 保留已有对外 URL；确需迁移时同时处理原链接。
6. 运行构建并修复所有不存在的链接、锚点和资源。

## 预览阅读与交互

在 `docs/` 执行本机预览：

```sh
npm run dev -- --port 4321
```

需要从同一局域网管理电脑访问时，显式监听该主机网络：

```sh
npm run dev -- --host 0.0.0.0 --port 4321
```

浏览器打开 `http://<文档主机IP>:4321/SmartDesktopMimi/`。检查长代码、表格、章节跳转和移动布局。站内搜索使用正式构建索引时，还应对构建预览或上线站点验证。

## 提交并触发自动部署

完成工程变更记录和本地检查后，将正文与相关文件提交到 `main` 并推送。工作流 `.github/workflows/docs.yml` 在 main 文档变更时构建与部署；PR 只构建。

仓库 **Settings → Pages → Source** 应为 **GitHub Actions**。部署工作流使用 Pages 权限和 `github-pages` 环境，不需要产品签名密钥或 Tina SDK。

项目地址为 [SmartDesktopMimi 文档站](https://dshanpi.github.io/SmartDesktopMimi/)。配置中的 base `/SmartDesktopMimi` 必须与项目路径一致。

## 在公网复核新版本

1. 确认本次提交对应 Actions 的 build/deploy 成功。
2. 打开公网首页与被修改的内页，确认正文已更新。
3. 检查中文搜索能找到新增内容，并能跳转到相应章节。
4. 在 HTTPS 页面复制命令，核对完整文本。
5. 检查图片、Mermaid、浅色/深色和手机导航。
6. 记录部署提交、工作流地址与实际检查结果。

部署失败时查看该次工作流日志并修复；构建成功但正文未更新时，先核对实际部署提交和资源地址。完成后停止临时预览服务。

参考：[EinkOS 文档源码](https://github.com/dshanpi/EinkOS/tree/main/docs)。本站产品事实、命令和版本均来自 SmartDesktopMimi。
