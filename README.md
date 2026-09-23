# AI-DeskTopBox 解决方案展示站

面向开发者、方案商与技术决策者的静态解决方案页面。内容以 `~/AI-DeskTopBox/docs/solution/website-content-kit.md` 为叙事基线，按“平台价值 → 桌面智能体验 → 方案蓝图 → 能力域 → 产品场景 → 实现证据 → 接入路径 → 交付边界”组织。

页面明确区分 AI-DeskTopBox 平台与 Smart Desktop Mimi 参考产品，也明确区分 A133 已实现基线、其他 SoC 适配接口和目标项目仍需完成的硬件验证。

## 本地预览

本项目没有构建步骤。在当前 TinaSDK 工作区中运行：

```bash
cd design/solution-site
python3 -m http.server 4173
```

然后访问 <http://127.0.0.1:4173/>。

## 项目结构

- `index.html`：解决方案叙事、能力边界和无障碍语义结构。
- `styles.css`：桌面、平板和手机端响应式视觉。
- `script.js`：移动导航与实机截图切换。
- `AGENTS.md`：站点专用 UI、交互和浏览器验收约束。
- `assets/`：参考产品概念图和 A133 真实运行截图。
- `docs/`：随站点提供的方案材料。

## 素材真实性

- 首屏直接使用站点原有的 `product-front.png` 与 `product-back.png` 产品正反面原图；产品场景区完整使用同一产品的 `scene-ai.webp`、`scene-hdmi.webp` 与 `scene-kvm.webp`，不使用文档素材包中的其他设备概念图。
- `ui-live-desktop.png`、`ui-ai-hdmi-mcp.png` 与 `ui-applications.png` 是从 `~/AI-DeskTopBox/artifacts/ui-screenshots-20260916-054316/` 原样复制的 A133 浏览器 UI 截图。
- `ui-device-desktop.png`、`ui-device-ai-chat.png`、`ui-device-all-apps.png`、`ui-device-bluetooth.png` 与 `ui-device-update.png` 是从 `~/AI-DeskTopBox/docs/debug-evidence/a133-en-us-20260906/` 原样复制的设备端 UI 截图。
- 产品图不做裁切、重绘、调色或重新压缩；功能范围以仓库实现、文档和目标硬件验证结果为准。

## 部署

`website` 分支只保存网站源代码，不包含 SmartDesktopMimi 产品源码。每次推送后，GitHub Actions 会自动校验并生成 `_site`，再将纯静态产物发布到 `gh-pages` 分支：

<https://dshanpi.github.io/SmartDesktopMimi/>

首次发布前，需要在仓库 **Settings → Pages → Build and deployment** 中选择 **Deploy from a branch**，分支选择 **gh-pages / (root)**。上线前仍需确认域名、备案信息、隐私政策和商标信息。
