# 维护与发布文档站

本站参考 EinkOS 的 Astro/Starlight 文档结构，使用独立中文教程、分组导航、站内搜索、代码复制、主题切换、目录和 Mermaid 图。最终托管于 GitHub Pages。

## 目录约定

```text
docs/
├── tutorial/              # 教程正文：唯一人工编辑来源
├── routes.mjs            # 页面映射、导航、公开图片来源
├── astro.config.mjs      # site / base / Starlight
├── source-links.mjs      # Markdown 相对链接转换
├── src/plugins/          # 链接与 Mermaid 转换
├── src/styles/custom.css # 主题样式
├── scripts/              # 内容生成和站点检查
├── public/enhance.js     # 图表运行时
└── package-lock.json     # 固定文档依赖
```

`src/content/docs/`、`dist/`、生成图片与 vendor 文件都由构建生成，不手工编辑、不提交。历史工程文档保留在原位置，通过源码链接查阅。

## 在新的 Ubuntu 主机构建

需要 Node.js ≥22.12（CI 使用 Node 24）。从 `main` 获取包含本站的源码：

```sh
git clone https://github.com/dshanpi/SmartDesktopMimi.git
cd SmartDesktopMimi/docs
node --version
npm ci
npm run build
```

`npm ci` 按 lockfile 安装；构建前生成 Starlight 内容并运行链接转换测试；构建后检查页面、站内链接、锚点和静态资源。输出在 `docs/dist/`，搜索索引由 Starlight/Pagefind 构建。

预览：

```sh
npm run dev -- --host 0.0.0.0 --port 4321
```

浏览器访问 `http://<文档主机IP>:4321/SmartDesktopMimi/`。默认仅本机预览时省略 `--host 0.0.0.0`。预览主机不是设备，不在这里执行板端调试命令。

## 新增一章

1. 在 `docs/tutorial/` 新建 Markdown，第一行为一个 `# 标题`。
2. 在 `routes.mjs` 添加 source、slug、group、title。
3. 同目录章节使用 `chapter.md` 相对链接；站点会转成带 `/SmartDesktopMimi/` 的路由。
4. 源码依据使用固定提交链接，避免文档与未来 main 漂移。
5. 图片登记公开来源，保留版本与来源说明。
6. 运行构建，查看失败目标并修复，预览长表格、代码块和移动布局。

## GitHub Pages 自动部署

工作流为 `.github/workflows/docs.yml`。仓库 Settings → Pages 的 Source 选择 **GitHub Actions**。`main` 的文档改动推送后构建并部署；PR 只构建，不部署。

构建 job 只需 `contents: read`；部署 job 单独拥有 `pages: write` 和 `id-token: write`，目标环境是 `github-pages`。不使用产品固件发布密钥或私有 SDK。

部署地址固定为：<https://dshanpi.github.io/SmartDesktopMimi/>。项目站必须保留 base `/SmartDesktopMimi`，否则首页可能能开但 CSS、图片和搜索失效。

## 发布后的验证

确认 Actions build/deploy 成功，打开公网首页和至少一个内页，检查导航、中文搜索、代码复制、图片、Mermaid、浅色/深色与移动布局。必要时从 Ubuntu 对所有页面和资源进行 HTTP 检查，核对实际部署提交。

仅有 `git push` 成功还不算发布完成；本地 build 成功也不代表线上内容已更新。失败时保留构建日志，按具体错误修复，不触发产品发布流程。

参考：[EinkOS 文档源码](https://github.com/dshanpi/EinkOS/tree/main/docs)。本站内容和产品状态基于小咪自身记录编写。
