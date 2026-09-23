#!/usr/bin/env python3
"""Build the Smart Desktop Mimi customer solution brief as a six-page A4 PDF."""

from __future__ import annotations

import argparse
from pathlib import Path

from reportlab.lib.colors import Color, HexColor, white
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader


ROOT = Path(__file__).resolve().parents[1]
SOLUTION_DIR = ROOT / "docs" / "solution"
ASSETS_DIR = SOLUTION_DIR / "assets"
DEFAULT_OUTPUT = SOLUTION_DIR / "smart-desktop-mimi-solution-brief.pdf"

FONT_REGULAR = str(ROOT / "apps/lv_port_linux/src/ui/font/SarasaUiSC-Regular.ttf")
FONT_BOLD = str(ROOT / "apps/lv_port_linux/src/ui/font/SarasaUiSC-SemiBold.ttf")
FONT_NAME = "MimiSans"
FONT_BOLD_NAME = "MimiSansBold"

INK = HexColor("#243039")
DEEP = HexColor("#123b43")
CYAN = HexColor("#078ca4")
CYAN_LIGHT = HexColor("#e7f6f8")
AMBER = HexColor("#dba646")
MUTED = HexColor("#65737a")
LINE = HexColor("#dbe5e7")
PAPER = HexColor("#fbfdfd")
PAGE_BG = HexColor("#ffffff")

PAGE_W, PAGE_H = A4
MARGIN = 15 * mm
CONTENT_W = PAGE_W - 2 * MARGIN


def register_fonts() -> None:
    """Register the product's Latin/CJK TrueType fonts for portable embedding."""
    pdfmetrics.registerFont(TTFont(FONT_NAME, FONT_REGULAR))
    pdfmetrics.registerFont(TTFont(FONT_BOLD_NAME, FONT_BOLD))


def wrap_text(text: str, font: str, size: float, max_width: float) -> list[str]:
    lines: list[str] = []
    current = ""
    for char in text:
        if char == "\n":
            lines.append(current)
            current = ""
            continue
        candidate = current + char
        if current and pdfmetrics.stringWidth(candidate, font, size) > max_width:
            lines.append(current.rstrip())
            current = char.lstrip()
        else:
            current = candidate
    if current or not lines:
        lines.append(current.rstrip())
    return lines


def draw_text(
    pdf: canvas.Canvas,
    text: str,
    x: float,
    y: float,
    width: float,
    *,
    font: str = FONT_NAME,
    size: float = 10,
    leading: float | None = None,
    color: Color = INK,
) -> float:
    leading = leading or size * 1.5
    pdf.setFont(font, size)
    pdf.setFillColor(color)
    for line in wrap_text(text, font, size, width):
        pdf.drawString(x, y, line)
        y -= leading
    return y


def draw_bullets(
    pdf: canvas.Canvas,
    items: list[str],
    x: float,
    y: float,
    width: float,
    *,
    size: float = 9,
    gap: float = 2.2 * mm,
) -> float:
    for item in items:
        pdf.setFillColor(CYAN)
        pdf.circle(x + 1.1 * mm, y + 1.4 * mm, 0.8 * mm, fill=1, stroke=0)
        y = draw_text(pdf, item, x + 4 * mm, y, width - 4 * mm, size=size, leading=size * 1.45)
        y -= gap
    return y


def draw_eyebrow(pdf: canvas.Canvas, text: str, y: float) -> None:
    pdf.setFillColor(CYAN)
    pdf.setFont(FONT_BOLD_NAME, 8.7)
    pdf.drawString(MARGIN, y, text)


def draw_title(pdf: canvas.Canvas, text: str, y: float, size: float = 22) -> float:
    return draw_text(
        pdf,
        text,
        MARGIN,
        y,
        CONTENT_W,
        font=FONT_BOLD_NAME,
        size=size,
        leading=size * 1.25,
        color=DEEP,
    )


def draw_footer(pdf: canvas.Canvas, left: str, page_number: int) -> None:
    pdf.setStrokeColor(LINE)
    pdf.line(MARGIN, 11 * mm, PAGE_W - MARGIN, 11 * mm)
    pdf.setFont(FONT_NAME, 7.5)
    pdf.setFillColor(MUTED)
    pdf.drawString(MARGIN, 7 * mm, left)
    pdf.drawRightString(PAGE_W - MARGIN, 7 * mm, f"{page_number:02d}")


def draw_card(
    pdf: canvas.Canvas,
    x: float,
    y_top: float,
    width: float,
    height: float,
    title: str,
    body: str,
) -> None:
    y = y_top - height
    pdf.setFillColor(PAPER)
    pdf.setStrokeColor(LINE)
    pdf.roundRect(x, y, width, height, 3.5 * mm, fill=1, stroke=1)
    pdf.setFillColor(CYAN)
    pdf.roundRect(x, y, 2.2 * mm, height, 1.1 * mm, fill=1, stroke=0)
    draw_text(
        pdf,
        title,
        x + 6 * mm,
        y_top - 7 * mm,
        width - 11 * mm,
        font=FONT_BOLD_NAME,
        size=11,
        leading=13,
        color=DEEP,
    )
    draw_text(
        pdf,
        body,
        x + 6 * mm,
        y_top - 14 * mm,
        width - 11 * mm,
        size=8.7,
        leading=12.5,
        color=INK,
    )


def draw_image_crop(
    pdf: canvas.Canvas, path: Path, x: float, y: float, width: float, height: float
) -> None:
    image = ImageReader(str(path))
    source_w, source_h = image.getSize()
    source_ratio = source_w / source_h
    target_ratio = width / height
    if source_ratio > target_ratio:
        draw_h = height
        draw_w = height * source_ratio
        draw_x = x - (draw_w - width) / 2
        draw_y = y
    else:
        draw_w = width
        draw_h = width / source_ratio
        draw_x = x
        draw_y = y - (draw_h - height) / 2
    pdf.saveState()
    clip_path = pdf.beginPath()
    clip_path.rect(x, y, width, height)
    pdf.clipPath(clip_path, stroke=0, fill=0)
    pdf.drawImage(image, draw_x, draw_y, draw_w, draw_h, preserveAspectRatio=True)
    pdf.restoreState()


def new_page(pdf: canvas.Canvas) -> None:
    pdf.setFillColor(PAGE_BG)
    pdf.rect(0, 0, PAGE_W, PAGE_H, fill=1, stroke=0)


def page_cover(pdf: canvas.Canvas) -> None:
    new_page(pdf)
    hero_h = 123 * mm
    draw_image_crop(
        pdf,
        ASSETS_DIR / "smart-desktop-mimi-hero.png",
        0,
        PAGE_H - hero_h,
        PAGE_W,
        hero_h,
    )
    y = PAGE_H - hero_h - 13 * mm
    draw_eyebrow(pdf, "MIMI AI DESKTOP SOLUTION · V1.0", y)
    y -= 10 * mm
    y = draw_text(
        pdf,
        "智能桌面小咪",
        MARGIN,
        y,
        CONTENT_W,
        font=FONT_BOLD_NAME,
        size=28,
        leading=33,
        color=DEEP,
    )
    y = draw_text(
        pdf,
        "带屏桌面 AI 终端解决方案",
        MARGIN,
        y - 1 * mm,
        CONTENT_W,
        font=FONT_BOLD_NAME,
        size=19,
        leading=23,
        color=DEEP,
    )
    y -= 5 * mm
    y = draw_text(
        pdf,
        "硬件 + 系统软件 + AI/IoT 接入 + 量产交付",
        MARGIN,
        y,
        CONTENT_W,
        font=FONT_BOLD_NAME,
        size=12,
        leading=17,
        color=CYAN,
    )
    pdf.setStrokeColor(AMBER)
    pdf.setLineWidth(2)
    pdf.line(MARGIN, y - 4 * mm, MARGIN, y - 20 * mm)
    draw_text(
        pdf,
        "面向智能硬件品牌、方案商和行业客户的可贴牌参考设计。\n概念图以最终量产实物为准。",
        MARGIN + 4 * mm,
        y - 7 * mm,
        CONTENT_W - 4 * mm,
        size=9,
        leading=13,
        color=MUTED,
    )
    draw_footer(pdf, "Smart Desktop Mimi 1.0.7 · 2026-09-14", 1)
    pdf.showPage()


def page_value(pdf: canvas.Canvas) -> None:
    new_page(pdf)
    draw_eyebrow(pdf, "WHY MIMI", PAGE_H - MARGIN)
    y = draw_title(pdf, "客户买到的，不只是一块主板", PAGE_H - 28 * mm)
    y = draw_text(
        pdf,
        "智能桌面小咪把板级驱动、桌面 UI、语音交互、设备云、手机端配置、量产授权和升级恢复整合为一个可交付的平台。",
        MARGIN,
        y - 3 * mm,
        CONTENT_W,
        size=11.5,
        leading=17,
        color=INK,
    )
    gap = 5 * mm
    card_w = (CONTENT_W - gap) / 2
    card_h = 39 * mm
    row1 = y - 6 * mm
    draw_card(pdf, MARGIN, row1, card_w, card_h, "快速形成样机", "已有可运行参考整机、固件和构建链路，减少从 BSP 与驱动开始的重复开发。")
    draw_card(pdf, MARGIN + card_w + gap, row1, card_w, card_h, "统一用户体验", "屏幕、语音、网络、蓝牙、状态灯与手机端配置在同一产品状态机下工作。")
    row2 = row1 - card_h - gap
    draw_card(pdf, MARGIN, row2, card_w, card_h, "面向量产设计", "每台设备使用独立身份，支持工厂写入、版本审计、恢复镜像和 OTA 回滚。")
    draw_card(pdf, MARGIN + card_w + gap, row2, card_w, card_h, "持续差异化", "品牌 UI、角色、音色、知识、用户 APP、MCP 工具和行业流程均可分层扩展。")
    band_y = row2 - card_h - 8 * mm
    pdf.setFillColor(DEEP)
    pdf.roundRect(MARGIN, band_y - 35 * mm, CONTENT_W, 35 * mm, 4 * mm, fill=1, stroke=0)
    draw_text(pdf, "一句话定位", MARGIN + 6 * mm, band_y - 9 * mm, CONTENT_W - 12 * mm, font=FONT_BOLD_NAME, size=11, color=HexColor("#8fe5ef"))
    draw_text(pdf, "客户提供品牌和销售场景，我们提供可量产的桌面 AI 软硬件底座。", MARGIN + 6 * mm, band_y - 19 * mm, CONTENT_W - 12 * mm, font=FONT_BOLD_NAME, size=12.5, leading=18, color=white)
    draw_footer(pdf, "智能桌面小咪解决方案", 2)
    pdf.showPage()


def page_architecture(pdf: canvas.Canvas) -> None:
    new_page(pdf)
    draw_eyebrow(pdf, "ARCHITECTURE & PACKAGES", PAGE_H - MARGIN)
    draw_title(pdf, "从设备到云端的完整交付", PAGE_H - 28 * mm)
    box_y = PAGE_H - 94 * mm
    gap = 8 * mm
    box_w = (CONTENT_W - 2 * gap) / 3
    architecture = [
        ("用户与品牌", "涂鸦智能 / OEM App\n角色、音色与内容"),
        ("小咪系统", "UI / AI / AEC / APP\n身份、OTA、诊断"),
        ("A133 硬件", "屏幕 / 音频 / 网络\nHDMI / HID / 灯效"),
    ]
    for index, (title, body) in enumerate(architecture):
        x = MARGIN + index * (box_w + gap)
        pdf.setFillColor(CYAN_LIGHT)
        pdf.setStrokeColor(HexColor("#c8e3e7"))
        pdf.roundRect(x, box_y, box_w, 37 * mm, 4 * mm, fill=1, stroke=1)
        draw_text(pdf, title, x + 4 * mm, box_y + 27 * mm, box_w - 8 * mm, font=FONT_BOLD_NAME, size=11, color=DEEP)
        draw_text(pdf, body, x + 4 * mm, box_y + 17 * mm, box_w - 8 * mm, size=8.5, leading=13, color=INK)
        if index < 2:
            pdf.setFont(FONT_BOLD_NAME, 15)
            pdf.setFillColor(AMBER)
            pdf.drawCentredString(x + box_w + gap / 2, box_y + 16 * mm, "→")
    package_y = box_y - 13 * mm
    packages = [
        ("标准方案版", "快速验证市场", ["参考主板或整机", "标准系统镜像", "涂鸦智能面板", "烧录与恢复资料"]),
        ("品牌定制版", "形成品牌产品", ["品牌 UI 与启动视觉", "角色、音色与知识", "OEM App 支持", "结构与声学适配"]),
        ("行业解决方案版", "进入业务流程", ["行业知识与 MCP", "HDMI/IPKVM 控制", "设备管理与策略", "运维 SLA"]),
    ]
    package_h = 88 * mm
    for index, (title, subtitle, bullets) in enumerate(packages):
        x = MARGIN + index * (box_w + gap)
        pdf.setFillColor(PAPER)
        pdf.setStrokeColor(LINE)
        pdf.roundRect(x, package_y - package_h, box_w, package_h, 4 * mm, fill=1, stroke=1)
        draw_text(pdf, title, x + 5 * mm, package_y - 9 * mm, box_w - 10 * mm, font=FONT_BOLD_NAME, size=12, color=DEEP)
        draw_text(pdf, subtitle, x + 5 * mm, package_y - 19 * mm, box_w - 10 * mm, font=FONT_BOLD_NAME, size=9, color=CYAN)
        draw_bullets(pdf, bullets, x + 5 * mm, package_y - 31 * mm, box_w - 10 * mm, size=8.3, gap=2.4 * mm)
    draw_footer(pdf, "硬件 + 软件 + 云 + 交付", 3)
    pdf.showPage()


def page_productization(pdf: canvas.Canvas) -> None:
    new_page(pdf)
    draw_eyebrow(pdf, "PRODUCTIZATION", PAGE_H - MARGIN)
    y = draw_title(pdf, "样机成功之后，必须经过量产质量门", PAGE_H - 28 * mm)
    rows = [
        ("启动与恢复", "连续 100 次冷启动；异常状态可按标准流程恢复"),
        ("网络与云", "断网、弱网、路由器重启；绑定、MQTT 与 DP 自动恢复"),
        ("AI 与音频", "72 小时连续运行；统计中断、重启、AEC 自激和误打断"),
        ("工厂一致性", "逐台检测屏幕、触摸、麦克风、扬声器、网络、灯效和身份"),
        ("OTA 安全", "下载中断、断电、校验失败、升级成功与失败回滚"),
        ("凭据安全", "一机一密；通用镜像、日志和仓库均不包含设备密钥"),
    ]
    table_top = y - 5 * mm
    row_h = 16 * mm
    for index, (title, body) in enumerate(rows):
        top = table_top - index * row_h
        pdf.setFillColor(PAPER if index % 2 == 0 else PAGE_BG)
        pdf.setStrokeColor(LINE)
        pdf.rect(MARGIN, top - row_h, CONTENT_W, row_h, fill=1, stroke=1)
        draw_text(pdf, title, MARGIN + 4 * mm, top - 6 * mm, 34 * mm, font=FONT_BOLD_NAME, size=9.2, color=DEEP)
        draw_text(pdf, body, MARGIN + 41 * mm, top - 6 * mm, CONTENT_W - 45 * mm, size=8.5, leading=12, color=INK)
    y = table_top - len(rows) * row_h - 9 * mm
    draw_text(pdf, "120 天小批量路线", MARGIN, y, CONTENT_W, font=FONT_BOLD_NAME, size=13, color=DEEP)
    y -= 9 * mm
    timeline = [
        ("1～30 天", "冻结 SKU、BOM、工装与功能边界"),
        ("31～60 天", "20 台样机、稳定性测试、客户试用"),
        ("61～90 天", "结构声学修正、认证与工厂流程"),
        ("91～120 天", "百台试产、激活返修与客户案例"),
    ]
    step_gap = 3 * mm
    step_w = (CONTENT_W - 3 * step_gap) / 4
    for index, (title, body) in enumerate(timeline):
        x = MARGIN + index * (step_w + step_gap)
        pdf.setFillColor(HexColor("#f2f8f8"))
        pdf.rect(x, y - 39 * mm, step_w, 39 * mm, fill=1, stroke=0)
        pdf.setFillColor(CYAN)
        pdf.rect(x, y - 2 * mm, step_w, 2 * mm, fill=1, stroke=0)
        draw_text(pdf, title, x + 3 * mm, y - 10 * mm, step_w - 6 * mm, font=FONT_BOLD_NAME, size=9, color=DEEP)
        draw_text(pdf, body, x + 3 * mm, y - 20 * mm, step_w - 6 * mm, size=7.8, leading=11.5, color=INK)
    draw_footer(pdf, "先验证，再开模，再备货", 4)
    pdf.showPage()


def page_storyboard(pdf: canvas.Canvas) -> None:
    new_page(pdf)
    draw_eyebrow(pdf, "DEMO STORYBOARD", PAGE_H - MARGIN)
    draw_title(pdf, "用 60 秒讲清楚产品价值", PAGE_H - 28 * mm)
    image_h = 93 * mm
    draw_image_crop(
        pdf,
        ASSETS_DIR / "smart-desktop-mimi-storyboard.png",
        MARGIN,
        PAGE_H - 139 * mm,
        CONTENT_W,
        image_h,
    )
    y = PAGE_H - 149 * mm
    gap = 5 * mm
    card_w = (CONTENT_W - gap) / 2
    card_h = 39 * mm
    draw_card(pdf, MARGIN, y, card_w, card_h, "01 开机与唤醒", "展示真实硬件冷启动，并使用当前交付固件实际支持的唤醒词。")
    draw_card(pdf, MARGIN + card_w + gap, y, card_w, card_h, "02 完整自然对话", "用固定英文问题验证识别、文字流、完整语音输出和状态反馈。")
    y -= card_h + gap
    draw_card(pdf, MARGIN, y, card_w, card_h, "03 人声打断", "展示用户可自然插话，同时说明普通环境声不应频繁误打断。")
    draw_card(pdf, MARGIN + card_w + gap, y, card_w, card_h, "04 手机配置", "展示设备在线、音色或角色真正下发，再快速扫过扩展功能。")
    draw_footer(pdf, "概念图以最终量产实物为准", 5)
    pdf.showPage()


def page_cooperation(pdf: canvas.Canvas) -> None:
    new_page(pdf)
    draw_eyebrow(pdf, "COOPERATION", PAGE_H - MARGIN)
    y = draw_title(pdf, "从付费样机开始合作", PAGE_H - 28 * mm)
    gap = 5 * mm
    card_w = (CONTENT_W - gap) / 2
    card_h = 37 * mm
    row1 = y - 5 * mm
    draw_card(pdf, MARGIN, row1, card_w, card_h, "样机验证", "标准功能、客户场景和云服务成本验证。")
    draw_card(pdf, MARGIN + card_w + gap, row1, card_w, card_h, "项目定制", "冻结 UI、结构、声学、App、AI 与验收范围，按 NRE 交付。")
    row2 = row1 - card_h - gap
    draw_card(pdf, MARGIN, row2, card_w, card_h, "批量供货", "主板或整机单价 + 每台软件授权 + AI/云服务套餐。")
    draw_card(pdf, MARGIN + card_w + gap, row2, card_w, card_h, "持续服务", "按年提供 OTA、安全维护、版本升级、诊断与 SLA。")
    y = row2 - card_h - 10 * mm
    pdf.setFillColor(DEEP)
    pdf.roundRect(MARGIN, y - 42 * mm, CONTENT_W, 42 * mm, 4 * mm, fill=1, stroke=0)
    draw_text(pdf, "首轮合作需要确认", MARGIN + 6 * mm, y - 10 * mm, CONTENT_W - 12 * mm, font=FONT_BOLD_NAME, size=11, color=HexColor("#8fe5ef"))
    draw_text(pdf, "目标用户与销售地区、预计数量、产品形态、语言与唤醒词、AI 角色和内容、App 路线、认证责任、交付时间与预算。", MARGIN + 6 * mm, y - 21 * mm, CONTENT_W - 12 * mm, size=9, leading=14, color=white)
    y -= 55 * mm
    draw_text(pdf, "智能桌面小咪", MARGIN, y, CONTENT_W, font=FONT_BOLD_NAME, size=24, leading=30, color=DEEP)
    draw_text(pdf, "从可工作的样机，走向可交付、可维护、可持续经营的 AI 硬件产品。", MARGIN, y - 13 * mm, CONTENT_W, font=FONT_BOLD_NAME, size=12, leading=18, color=CYAN)
    draw_text(pdf, "商务联系人：________________　联系方式：________________", MARGIN, y - 36 * mm, CONTENT_W, size=9, color=MUTED)
    draw_footer(pdf, "Mimi AI Desktop Solution", 6)
    pdf.showPage()


def build(output: Path) -> None:
    register_fonts()
    output.parent.mkdir(parents=True, exist_ok=True)
    pdf = canvas.Canvas(str(output), pagesize=A4, pageCompression=1)
    pdf.setTitle("智能桌面小咪解决方案")
    pdf.setAuthor("Smart Desktop Mimi")
    pdf.setSubject("带屏桌面 AI 终端软硬件解决方案")
    page_cover(pdf)
    page_value(pdf)
    page_architecture(pdf)
    page_productization(pdf)
    page_storyboard(pdf)
    page_cooperation(pdf)
    pdf.save()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    build(args.output.resolve())
    print(args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
