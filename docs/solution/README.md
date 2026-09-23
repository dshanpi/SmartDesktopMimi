# 智能桌面小咪解决方案资料包

本目录用于保存“智能桌面小咪（Smart Desktop Mimi）”从参考产品走向可销售
软硬件解决方案所需的客户材料和内部执行资料。

## 资料入口

- [解决方案网站内容素材包](website-content-kit.md)：面向开发者与方案商的中英双语
  长页面文案、功能矩阵、开发入口、配图映射、SEO、CTA 与能力边界。
- [客户版解决方案](smart-desktop-mimi-solution.md)：产品定位、能力、交付版本、
  系统架构、验收边界与合作方式。
- [商业化执行计划](commercialization-plan.md)：目标客户、收入结构、报价方法、
  120 天小批量路线和经营指标。
- [图片版演示分镜](demo-storyboard.md)：当前用图片代替视频的 60 秒演示脚本。
- [客户需求确认表](customer-requirements-template.md)：首次沟通、方案评估和分项报价的输入模板。
- [概念图生成记录](image-prompts.md)：图片用途、生成方式和可复用提示词。
- `smart-desktop-mimi-solution-brief.html`：可打印的客户简版画册源文件。
- `smart-desktop-mimi-solution-brief.pdf`：固定六页 A4 的客户简版 PDF。

PDF 使用仓库内的可复现脚本生成：

```sh
python3 scripts/build_solution_brief.py
```

脚本依赖 ReportLab，并复用产品 UI 自带的 Sarasa Latin/CJK 字体；HTML 版本用于
浏览器预览，PDF 以脚本生成结果为准。

## 素材说明

`assets/` 下的产品图为方案阶段概念视觉，不代表已冻结的工业设计、结构尺寸或
量产外观。对外使用时必须保留“概念图，以最终量产实物为准”的说明。

技术能力与当前实现状态以仓库中的
[平台功能总览](../platform-feature-overview.md)和
[平台验证记录](../platform-validation.md)为准；销售材料不得覆盖真实验证结论。
