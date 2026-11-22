# -*- coding: utf-8 -*-
"""Generate the Benchmark 2.0 Ultimate technical whitepaper PDF."""
import os
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib.colors import HexColor, white, black
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT, TA_CENTER, TA_RIGHT
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph,
                                Spacer, PageBreak, Table, TableStyle, KeepTogether,
                                Flowable, NextPageTemplate)
from reportlab.graphics.shapes import Drawing, Rect, String, Line, Polygon, Circle
from reportlab.graphics import renderPDF

# ---------------------------------------------------------------- fonts / palette
FONT_PATH = "/System/Library/Fonts/STHeiti Medium.ttc"
pdfmetrics.registerFont(TTFont("CJK", FONT_PATH, subfontIndex=0))
pdfmetrics.registerFontFamily("CJK", normal="CJK", bold="CJK",
                              italic="CJK", boldItalic="CJK")

NAVY = HexColor("#0B1220")
NAVY2 = HexColor("#111B2E")
INK = HexColor("#172033")
MUTED = HexColor("#5B6B82")
CYAN = HexColor("#0891B2")
BLUE = HexColor("#2563EB")
GREEN = HexColor("#059669")
AMBER = HexColor("#D97706")
RED = HexColor("#DC2626")
LINE = HexColor("#D7DEE8")
PALE = HexColor("#F3F7FB")
PALE2 = HexColor("#E8F3F7")

PAGE_W, PAGE_H = A4
M = 46
CONTENT_W = PAGE_W - 2 * M

# ---------------------------------------------------------------- styles
def st(name, size, leading=None, color=INK, align=TA_LEFT, space_before=0,
       space_after=6, font="CJK"):
    return ParagraphStyle(name, fontName=font, fontSize=size,
                          leading=leading or size * 1.45, textColor=color,
                          alignment=align, spaceBefore=space_before,
                          spaceAfter=space_after, wordWrap="CJK")

BODY = st("body", 10.3, 16.2, INK, space_after=7)
BODY_S = st("body_s", 9.1, 14.0, MUTED, space_after=5)
BULLET = st("bullet", 10.2, 15.6, INK, space_after=4)
H1 = st("h1", 22, 28, NAVY, space_before=2, space_after=9)
H2 = st("h2", 14.2, 19, NAVY, space_before=10, space_after=6)
H3 = st("h3", 11.3, 15, CYAN, space_before=7, space_after=4)
CAP = st("cap", 8.2, 11, MUTED, space_after=4)
COVER_H = st("cover_h", 31, 38, white, space_after=10)
COVER_SUB = st("cover_sub", 13, 19, HexColor("#A9C4DA"), space_after=12)
COVER_SMALL = st("cover_small", 9.2, 14, HexColor("#8FA9C0"))

# ---------------------------------------------------------------- drawing helpers
def txt(d, x, y, s, size=9, color=INK, anchor="start"):
    t = String(x, y, s, fontName="CJK", fontSize=size, fillColor=color)
    if anchor == "middle":
        t.textAnchor = "middle"
    elif anchor == "end":
        t.textAnchor = "end"
    d.add(t)

def box(d, x, y, w, h, fill, stroke=None, r=7, title=None, sub=None,
        tc=white, sc=None, size=9.2):
    d.add(Rect(x, y, w, h, rx=r, ry=r, fillColor=fill,
               strokeColor=stroke or fill, strokeWidth=0.8))
    if title:
        txt(d, x + w / 2, y + h / 2 + (5 if sub else 0), title,
            size, tc, "middle")
    if sub:
        txt(d, x + w / 2, y + h / 2 - 9, sub, 7.8,
            sc or HexColor("#DCE8F3"), "middle")

def arrow(d, x1, y1, x2, y2, color=MUTED, width=1.4):
    d.add(Line(x1, y1, x2, y2, strokeColor=color, strokeWidth=width))
    import math
    ang = math.atan2(y2 - y1, x2 - x1)
    L = 6.5
    p = Polygon([x2, y2,
                 x2 - L * math.cos(ang - 0.45), y2 - L * math.sin(ang - 0.45),
                 x2 - L * math.cos(ang + 0.45), y2 - L * math.sin(ang + 0.45)],
                fillColor=color, strokeColor=color)
    d.add(p)

def bar_chart(labels, values, unit, colors=None, width=CONTENT_W, height=160,
              log=False, ymax=None, title=None):
    d = Drawing(width, height)
    left, right, top, bottom = 42, 12, 20, 30
    plot_w = width - left - right
    plot_h = height - top - bottom
    if title:
        txt(d, 0, height - 12, title, 9.2, NAVY)
    if colors is None:
        colors = [CYAN] * len(values)
    if log:
        import math
        vmax = ymax or max(values) * 1.4
        vmin = 0.5
        lo, hi = math.log10(vmin), math.log10(vmax)
        def yy(v):
            return bottom + (math.log10(max(v, vmin)) - lo) / (hi - lo) * plot_h
        ticks = []
        p = 1
        while p <= vmax * 1.5:
            if p >= vmin:
                ticks.append(p)
            p *= 10
        for t in ticks:
            y = yy(t)
            d.add(Line(left, y, left + plot_w, y, strokeColor=LINE, strokeWidth=0.5))
            txt(d, left - 5, y - 2.5, f"{t:g}", 7, MUTED, "end")
        for i, (lab, v) in enumerate(zip(labels, values)):
            bw = plot_w / len(values) * 0.6
            x = left + plot_w / len(values) * (i + 0.2)
            y = yy(v)
            d.add(Rect(x, bottom, bw, max(1, y - bottom), fillColor=colors[i],
                       strokeColor=colors[i]))
            txt(d, x + bw / 2, y + 4, f"{v:.3g}", 7.2, INK, "middle")
            txt(d, x + bw / 2, bottom - 13, lab, 7.2, MUTED, "middle")
    else:
        vmax = ymax or max(values) * 1.22
        for i in range(5):
            y = bottom + plot_h * i / 4
            d.add(Line(left, y, left + plot_w, y, strokeColor=LINE, strokeWidth=0.5))
            txt(d, left - 5, y - 2.5, f"{vmax*i/4:.0f}", 7, MUTED, "end")
        for i, (lab, v) in enumerate(zip(labels, values)):
            bw = plot_w / len(values) * 0.6
            x = left + plot_w / len(values) * (i + 0.2)
            hh = plot_h * v / vmax
            d.add(Rect(x, bottom, bw, hh, fillColor=colors[i], strokeColor=colors[i]))
            txt(d, x + bw / 2, bottom + hh + 4, f"{v:.2f}", 7.2, INK, "middle")
            txt(d, x + bw / 2, bottom - 13, lab, 7.2, MUTED, "middle")
    txt(d, 0, 3, unit, 7.2, MUTED)
    return d

# ---------------------------------------------------------------- page furniture
def draw_cover(canvas, doc):
    canvas.saveState()
    canvas.setFillColor(NAVY)
    canvas.rect(0, 0, PAGE_W, PAGE_H, fill=1, stroke=0)
    # subtle circuit lines
    canvas.setStrokeColor(HexColor("#17304A"))
    canvas.setLineWidth(0.6)
    for y in (150, 190, 650, 690):
        canvas.line(0, y, PAGE_W, y)
    for x in (80, 110, 500, 530):
        canvas.line(x, 0, x, PAGE_H)
    canvas.setFillColor(CYAN)
    canvas.rect(M, 90, 92, 4, fill=1, stroke=0)
    canvas.setFillColor(HexColor("#0F2236"))
    canvas.circle(PAGE_W - 95, PAGE_H - 120, 52, fill=1, stroke=0)
    canvas.setFillColor(CYAN)
    canvas.circle(PAGE_W - 95, PAGE_H - 120, 7, fill=1, stroke=0)
    canvas.restoreState()

def draw_page(canvas, doc):
    canvas.saveState()
    canvas.setFont("CJK", 8)
    canvas.setFillColor(MUTED)
    canvas.drawString(M, PAGE_H - 28, "Benchmark 2.0 Ultimate  ·  技术白皮书")
    canvas.drawRightString(PAGE_W - M, PAGE_H - 28, "2026-11-22")
    canvas.setStrokeColor(LINE)
    canvas.setLineWidth(0.5)
    canvas.line(M, PAGE_H - 34, PAGE_W - M, PAGE_H - 34)
    canvas.line(M, 30, PAGE_W - M, 30)
    canvas.drawString(M, 18, "CPU Compute · Memory / Cache · macOS / Windows")
    canvas.drawRightString(PAGE_W - M, 18, f"{doc.page}")
    canvas.restoreState()

# ---------------------------------------------------------------- content helpers
def bullet(text):
    return Paragraph(text, BULLET, bulletText="•")

def section(title):
    return Paragraph(title, H1)

def h2(title):
    return Paragraph(title, H2)

def h3(title):
    return Paragraph(title, H3)

def p(text):
    return Paragraph(text, BODY)

def small(text):
    return Paragraph(text, BODY_S)

def callout(title, text, color=PALE2, border=CYAN):
    t = Table([[Paragraph(f"<b>{title}</b><br/>{text}", BODY)]],
              colWidths=[CONTENT_W])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), color),
        ("BOX", (0, 0), (-1, -1), 0.7, border),
        ("LEFTPADDING", (0, 0), (-1, -1), 12),
        ("RIGHTPADDING", (0, 0), (-1, -1), 12),
        ("TOPPADDING", (0, 0), (-1, -1), 10),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 10),
    ]))
    return t

def data_table(rows, widths, header=True, font=8.5):
    data = []
    for ri, r in enumerate(rows):
        color = white if (header and ri == 0) else INK
        data.append([Paragraph(str(c), st("cell", font, font * 1.45, color,
                                          TA_LEFT, 0, 0)) for c in r])
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [
        ("GRID", (0, 0), (-1, -1), 0.35, LINE),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]
    if header:
        style += [("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                  ("TEXTCOLOR", (0, 0), (-1, 0), white)]
        # table header paragraphs are dark; use a second table? simpler: dark bg + font white not applied to Paragraph
    t.setStyle(TableStyle(style))
    return t

# ---------------------------------------------------------------- build
class Whitepaper(BaseDocTemplate):
    def __init__(self, filename):
        BaseDocTemplate.__init__(self, filename, pagesize=A4,
            leftMargin=M, rightMargin=M, topMargin=52, bottomMargin=48,
            title="Benchmark 2.0 Ultimate 技术白皮书",
            author="Benchmark 2.0 Ultimate", subject="CPU compute benchmark")
        cover = PageTemplate(id="cover", frames=[Frame(M, 40, CONTENT_W, PAGE_H - 80, id="c")],
                             onPage=draw_cover)
        body = PageTemplate(id="body", frames=[Frame(M, 44, CONTENT_W, PAGE_H - 92, id="b")],
                            onPage=draw_page)
        self.addPageTemplates([cover, body])

story = []

# ---------------------------------------------------------------- cover
story.append(Spacer(1, 52))
story.append(Paragraph("BENCHMARK", st("cover_kicker", 12, 18, CYAN, TA_LEFT, 0, 4)))
story.append(Paragraph("2.0 Ultimate", COVER_H))
story.append(Paragraph("CPU 计算性能基准 · 技术白皮书", COVER_SUB))
story.append(Paragraph("寄存器驻留的纯计算微基准 · x86-64 / Apple Silicon · macOS / Windows",
                       COVER_SMALL))
# chip / bar motif
d = Drawing(450, 150)
d.add(Rect(0, 0, 450, 150, rx=14, ry=14, fillColor=HexColor("#0F2236"),
           strokeColor=HexColor("#1E3A52"), strokeWidth=0.8))
for i in range(8):
    x = 30 + i * 48
    d.add(Rect(x, 30, 30, 64, rx=4, ry=4,
               fillColor=HexColor("#0E7490") if i < 5 else HexColor("#155E75"),
               strokeColor=HexColor("#22D3EE"), strokeWidth=0.6))
for i in range(7):
    d.add(Line(45, 105 + i * 5, 405, 105 + i * 5, strokeColor=HexColor("#164E63"),
               strokeWidth=0.5))
d.add(Rect(410, 30, 14, 64, rx=4, ry=4, fillColor=CYAN))
story.append(d)
story.append(Spacer(1, 22))
story.append(Paragraph("ARCH  ·  NATIVE  ·  SYSTEM  ·  MEMORY", st("cover_tags", 9, 14,
                       HexColor("#9FB6CA"), TA_LEFT, 0, 6)))
story.append(Paragraph("2026-11-22", COVER_SMALL))
story.append(NextPageTemplate("body"))
story.append(PageBreak())

# ---------------------------------------------------------------- summary
story.append(section("01  摘要与关键结论"))
story.append(p("Benchmark 2.0 Ultimate 是一个只测量 CPU 计算吞吐的微基准。热循环全部驻留在寄存器和局部变量中，不流式访问大数组，因此不会把缓存或内存带宽混进 CPU 计算分。它的目标不是复刻 Cinebench 或 SPEC，而是提供一个可解释、可复现、能拆开看的计算能力标尺。"))
story.append(callout("一句话结论",
    "2.0 把“单核”“多核”“跨架构”“内存”拆成独立口径：多核分 = 单核分 × 实测倍率，ARCH 默认自动运行，Memory 模块完全独立，不污染 CPU 总分。"))

rows = [
    ["口径", "参考机 i7-9750H 冷机", "Apple M1 virtual（3 vCPU）", "用途"],
    ["ARCH（标量 1T）", "约 1000（实测 1078）", "991", "跨 x86 / ARM 架构"],
    ["NATIVE 单核", "1006", "893", "同架构向量能力"],
    ["NATIVE 多核", "5693", "2150", "整机多线程吞吐"],
    ["MULTI SCALE", "5.66x", "2.41x", "实测多核倍率"],
    ["OVERALL", "2394", "1385", "sqrt(单核 × 多核)"],
]
t = data_table(rows, [95, 120, 135, 150], font=8.3)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white),
                       ("BACKGROUND", (0, 1), (-1, -1), white)]))
story.append(t)
story.append(Spacer(1, 8))
story.append(small("注：i7 为一次冷机实测，存在热降频和运行波动；M1 为 GitHub Actions 3 vCPU 虚拟环境，不代表 8 核本机 M1。"))
story.append(h2("2.0 的关键变化"))
for x in [
    "<b>多核缩放</b>：MULTI SCORE 现在约等于 SINGLE × MULTI SCALE，12 线程不会再显示成和单核差不多。",
    "<b>ARCH 自动运行</b>：每次跑分自动追加标量单线程，不再需要 --arch；跨架构和原生向量分同时可见。",
    "<b>内存独立</b>：Memory 模块单独测量缓存/DRAM 延迟与带宽，不进入 CPU 参考分。",
    "<b>平台对齐</b>：macOS 自动选择 M1-M4 / Intel 二进制；Windows 提供 AVX2、通用 x86_64 和 ARM64 三个可执行文件。",
    "<b>版本边界</b>：2.0 的多核口径与 1.x 不同，1.x 的 MULTI / OVERALL 分数不能和 2.0 直接比较。",
]:
    story.append(bullet(x))
story.append(h2("怎么读这份文档"))
story.append(p("第 02~03 节说明设计边界；第 04~06 节是 kernel、测量方法和评分公式；第 07 节解释跨架构公平性；第 08 节是独立的 Memory 模块；第 09~12 节是交付、优缺点、复现和路线图。"))

# ---------------------------------------------------------------- design goals
story.append(PageBreak())
story.append(section("02  设计目标与非目标"))
story.append(h2("目标"))
for x in [
    "只测计算：所有 kernel 为寄存器驻留的算术循环。",
    "可解释：每一项都有明确的指令组合、操作计数和单位。",
    "可复现：自适应时长、多次采样中位数、固定参考常数。",
    "可拆解：ARCH、NATIVE、SYSTEM 分开，不用一个总分回答所有问题。",
    "跨平台：macOS / Windows，x86-64 / Apple Silicon。",
    "工程化：JSON、崩溃日志、自动降级、U 盘结果模式。",
]:
    story.append(bullet(x))
story.append(h2("非目标"))
for x in [
    "不测 GPU、NPU、磁盘、网络。",
    "不测大数组内存带宽和缓存命中率；这些由独立的 Memory 模块负责。",
    "不追求理论峰值 FLOPS，也不冒充整机性能。",
    "不做跨架构唯一总分：跨架构只看 ARCH，原生看 NATIVE / SYSTEM。",
]:
    story.append(bullet(x))
story.append(callout("为什么不用一个总分",
    "SIMD 宽度、核心数、功耗墙、编译器、OS 调度会同时影响结果。一个总分只能回答“某个加权 workload 谁快”，不能回答“谁的架构更强”或“整机谁更强”。2.0 用多个口径替代一个模糊总分。", HexColor("#FFF7ED"), AMBER))
story.append(h2("设计原则"))
rows = [
    ["原则", "具体做法", "代价"],
    ["可解释 > 覆盖广", "每项都有明确 kernel、操作计数、单位", "不覆盖真实应用的全部行为"],
    ["同工作量 > 峰值", "x86/ARM 做相同数量的 FLOP / sqrt", "x86 向量宽度优势仍存在"],
    ["独立口径 > 单一总分", "ARCH / NATIVE / SYSTEM / Memory 分开", "没有“一个数字”的爽感"],
    ["短时微基准 > 完整应用", "0.35 s × 3，寄存器驻留", "偏 boost，不是稳态"],
]
t = data_table(rows, [115, 235, 153], font=8.3)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)

# ---------------------------------------------------------------- architecture
story.append(PageBreak())
story.append(section("03  总体架构"))
story.append(p("程序是单文件 C，平台层只负责计时、线程、QoS、CPU 信息和缓存大小；计算、校准、评分和输出都在共享代码中。"))
d = Drawing(CONTENT_W, 190)
y = 108
box(d, 0, y, 86, 44, HexColor("#0F766E"), None, 7, "CLI / 平台", "macOS · Windows", size=8.8)
box(d, 100, y, 86, 44, HexColor("#0E7490"), None, 7, "Workloads", "6 个计算 kernel", size=8.8)
box(d, 200, y, 86, 44, HexColor("#1D4ED8"), None, 7, "校准 / 采样", "0.35 s × 3", size=8.8)
box(d, 300, y, 86, 44, HexColor("#6D28D9"), None, 7, "评分", "ARCH / NATIVE", size=8.8)
box(d, 400, y, 103, 44, HexColor("#B45309"), None, 7, "输出", "表格 · JSON", size=8.8)
for x in [86, 186, 286, 386]:
    arrow(d, x + 5, y + 22, x + 14, y + 22)
box(d, 90, 28, 150, 40, PALE2, HexColor("#BAE6FD"), 7, "Memory / Cache", "独立模块，不进入 CPU 总分", tc=NAVY, sc=MUTED, size=8.6)
box(d, 260, 28, 150, 40, PALE2, HexColor("#BAE6FD"), 7, "JSON / 报告", "版本化字段", tc=NAVY, sc=MUTED, size=8.6)
story.append(d)
story.append(h3("关键部件"))
for x in [
    "<b>平台层</b>：macOS 用 clock_gettime / sysctl / QoS；Windows 用 QPC / Win32 线程 / Registry / UCRT。",
    "<b>Workloads</b>：整数加、乘、串行 hash、FP32/FP64 FMA、FP64 sqrt。",
    "<b>测量引擎</b>：自适应迭代数、启动门闩、多次采样中位数。",
    "<b>评分引擎</b>：单核、原生多核、ARCH 标量、可选的 Memory 模块。",
    "<b>输出</b>：终端表格、JSON、崩溃日志；Windows 支持自动降级和 U 盘结果。",
]:
    story.append(bullet(x))
story.append(h2("模块边界"))
rows = [
    ["模块", "测量对象", "是否进入 CPU 分"],
    ["CPU Compute", "寄存器驻留的整数/浮点算术", "是：ARCH / NATIVE / SYSTEM"],
    ["Memory / Cache", "L1-L3/DRAM 延迟与顺序带宽", "否，完全独立"],
    ["Accelerator", "GPU / NPU（未来独立工具）", "否"],
    ["Storage / Network", "I/O（不做）", "否"],
]
t = data_table(rows, [110, 235, 158], font=8.4)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white),
                       ("BACKGROUND", (0, 1), (-1, 1), PALE2)]))
story.append(t)
story.append(small("一次 CPU 运行的顺序：读取 CPU/拓扑与缓存信息 -> 预热 -> NATIVE 单核 -> NATIVE 多核（门闩）-> ARCH 标量 -> 汇总 -> 可选 JSON。"))

# ---------------------------------------------------------------- kernels
story.append(PageBreak())
story.append(section("04  六个 CPU 计算负载"))
rows = [
    ["项目", "每轮操作", "主要资源", "单位"],
    ["INT64 add", "8 × 64-bit add", "4 个整数 ALU", "Gop/s"],
    ["INT64 mul", "8 × 64-bit imul", "整数乘法器", "Gop/s"],
    ["INT64 hash", "1 × SplitMix 链", "乘法/移位/异或延迟", "Mhash/s"],
    ["FP32 FMA", "64 次 lane-FMA", "AVX2/NEON FMA 端口", "GFLOP/s"],
    ["FP64 FMA", "32 次 lane-FMA", "FP64 FMA 端口", "GFLOP/s"],
    ["FP64 sqrt", "32 次开方", "sqrt/divider 单元", "Gsqrt/s"],
]
t = data_table(rows, [95, 140, 160, 80], font=8.4)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)
story.append(Spacer(1, 10))
story.append(h3("为什么要 8 / 16 条独立链"))
story.append(p("FMA 有 4~5 个周期延迟。如果只有一条累加链，CPU 大部分时间在等结果，测到的是延迟，不是吞吐。x86 用 8 条链覆盖 2 个 AVX2 FMA 端口；Apple Silicon 用 16 条链覆盖最多 4 个 NEON 管线。"))
# FMA width graphic
d = Drawing(CONTENT_W, 128)
txt(d, 0, 116, "同样 64 个 lane-FMA / 轮：x86 8 条 256-bit 指令，ARM 16 条 128-bit 指令", 8.2, NAVY)
for i in range(8):
    x = 18 + i * 58
    d.add(Rect(x, 72, 46, 26, rx=4, ry=4, fillColor=HexColor("#0E7490")))
    txt(d, x + 23, 80, "256b", 7, white, "middle")
txt(d, 0, 56, "x86 AVX2：8 条指令 → 64 lane-FMA", 8, MUTED)
for i in range(16):
    x = 18 + i * 29
    d.add(Rect(x, 20, 22, 22, rx=3, ry=3, fillColor=HexColor("#7C3AED")))
    txt(d, x + 11, 27, "128", 5.8, white, "middle")
txt(d, 0, 4, "Apple NEON：16 条指令 → 同样 64 lane-FMA（宽度是真实架构差异）", 8, MUTED)
story.append(d)
story.append(h3("防优化"))
for x in [
    "<b>noinline</b>：防止 kernel 被内联后重排。",
    "<b>空 asm 屏障</b>：每轮强制累加器写入寄存器约束，阻止闭式折叠。",
    "<b>volatile sink</b>：防止整个调用被 DCE。",
    "<b>关闭整数自动向量化</b>：整数项测标量 ALU，不被编译器改成 SIMD。",
    "<b>不用 -ffast-math</b>：sqrt 是真实硬件开方，不用近似倒数。",
]:
    story.append(bullet(x))
story.append(h2("为什么是这六项"))
for x in [
    "<b>整数加/乘</b>：覆盖最常见的 ALU 和乘法器吞吐，几乎不受缓存影响。",
    "<b>串行 hash</b>：强制依赖链，测整数混合的延迟，而不是并行吞吐。",
    "<b>FP32/FP64 FMA</b>：覆盖向量浮点主路径；两者分别看单精度和双精度执行单元。",
    "<b>FP64 sqrt</b>：单独压迫 sqrt/divider 单元，避免被 FMA 吞吐掩盖。",
]:
    story.append(bullet(x))
story.append(small("这六项不是完整应用模型；它们是可解释的“探针”。任何单项都不应单独代表 CPU 的全部性能。"))

# ---------------------------------------------------------------- measurement
story.append(PageBreak())
story.append(section("05  测量方法"))
story.append(h2("计时与校准"))
story.append(p("macOS 使用 CLOCK_MONOTONIC，Windows 使用 QPC。每个项目先从较小迭代数开始，测得耗时后按比例调整，使一次采样接近 0.35 秒；再运行 3 次取中位数。"))
d = Drawing(CONTENT_W, 112)
box(d, 0, 48, 98, 36, PALE2, HexColor("#BAE6FD"), 6, "小迭代试跑", tc=NAVY, sc=MUTED, size=8)
box(d, 118, 48, 98, 36, PALE2, HexColor("#BAE6FD"), 6, "按耗时缩放", tc=NAVY, sc=MUTED, size=8)
box(d, 236, 48, 98, 36, PALE2, HexColor("#BAE6FD"), 6, "目标 0.35 s", tc=NAVY, sc=MUTED, size=8)
box(d, 354, 48, 120, 36, PALE2, HexColor("#BAE6FD"), 6, "3 次取中位数", tc=NAVY, sc=MUTED, size=8)
for x in [98, 216, 334]:
    arrow(d, x + 3, 66, x + 17, 66)
txt(d, 0, 20, "计时区间只包含计算循环；线程创建、校准和 JIT 预热都在计时之外。", 8, MUTED)
story.append(d)
story.append(h2("多线程"))
story.append(p("多线程测试使用启动门闩：主线程创建 worker、等待全部就绪，再释放门闩并计时。线程启动与销毁不计入热循环。测试使用 wall time，总操作数 = 迭代数 × 线程数 × 每轮操作数。"))
story.append(h2("调度与异构核"))
story.append(p("macOS 为线程设置 USER_INTERACTIVE QoS，尽量让单核任务留在 P 核；Apple Silicon 显示 P/E 核，可用 --pcores 只跑 P 核。Windows 当前使用系统调度，不强制绑定。"))
story.append(callout("为什么还要中位数",
    "单次采样会被后台进程、频率切换、调度迁移污染。3 次取中位数比单次最大值更接近可持续水平；但仍是短时测试，不是 30 分钟稳态。"))
story.append(h2("误差来源与控制"))
rows = [
    ["误差源", "典型影响", "控制手段"],
    ["频率/Boost/热", "5~20%", "预热、中位数、冷却后复跑、--samples 5"],
    ["后台进程/调度迁移", "1~10%", "QoS、P 核模式、关闭后台任务"],
    ["定时器分辨率", "<1%", "CLOCK_MONOTONIC / QPC，目标 0.35 s"],
    ["编译器代码生成", "数个百分点", "noinline、asm 屏障、volatile sink"],
    ["线程启动/销毁", "多核可到数个百分点", "启动门闩，计时区间只含计算"],
    ["异构核调度", "可达 20%+", "显示 P/E；macOS QoS；--pcores 可选"],
]
t = data_table(rows, [130, 110, 263], font=8.2)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)
story.append(small("这些误差不是程序 bug，而是短时基准的物理限制。报告 JSON 时建议同时记录冷却状态、电源模式和后台负载。"))

# ---------------------------------------------------------------- scoring
story.append(PageBreak())
story.append(section("06  评分模型 2.0"))
story.append(p("2.0 的核心改动是多核分不再拿 12 线程吞吐去和 12 线程参考直接比，而是拿它和“单核参考”比，从而显出真实缩放。"))
story.append(h3("公式"))
for x in [
    "<b>单项单核分</b> = 1000 × 单核吞吐 / 单核参考吞吐",
    "<b>单项多核分</b> = 1000 × 多核吞吐 / 单核参考吞吐 = 单核分 × 该项目实测倍率",
    "<b>MULTI SCALE</b> = 6 项多核/单核倍率的几何平均",
    "<b>MULTI SCORE</b> = 6 项多核分的几何平均 ≈ SINGLE SCORE × MULTI SCALE",
    "<b>OVERALL</b> = sqrt(SINGLE SCORE × MULTI SCORE)",
    "<b>ARCH SCORE</b> = 5 项标量单核分的几何平均，独立展示",
]:
    story.append(bullet(x))
# score stack graphic
d = Drawing(CONTENT_W, 145)
box(d, 0, 92, 104, 38, HexColor("#0E7490"), None, 6, "SINGLE", "单核几何平均", size=8.5)
box(d, 122, 92, 104, 38, HexColor("#1D4ED8"), None, 6, "MULTI SCALE", "实测倍率", size=8.5)
box(d, 244, 92, 104, 38, HexColor("#6D28D9"), None, 6, "MULTI SCORE", "单核 × 倍率", size=8.5)
box(d, 366, 92, 120, 38, HexColor("#B45309"), None, 6, "OVERALL", "sqrt(单核 × 多核)", size=8.5)
arrow(d, 106, 111, 120, 111)
arrow(d, 228, 111, 242, 111)
arrow(d, 350, 111, 364, 111)
box(d, 0, 25, 150, 36, PALE2, HexColor("#BAE6FD"), 6, "ARCH SCORE", "标量 1T，跨架构", tc=NAVY, sc=MUTED, size=8.5)
box(d, 170, 25, 200, 36, PALE2, HexColor("#BAE6FD"), 6, "MEMORY MODULE", "独立，不进入 CPU 总分", tc=NAVY, sc=MUTED, size=8.5)
story.append(d)
story.append(h2("参考机"))
rows = [
    ["项目", "单核", "12 线程", "多核分"],
    ["INT64 add", "14.41", "81.76", "5678"],
    ["INT64 mul", "4.28", "23.70", "5537"],
    ["INT64 hash", "327.21", "3321.30", "10157"],
    ["FP32 FMA", "137.06", "665.90", "4861"],
    ["FP64 FMA", "66.98", "309.20", "4615"],
    ["FP64 sqrt", "1.40", "7.93", "5664"],
    ["综合", "1000", "—", "5862"],
]
t = data_table(rows, [100, 100, 100, 100], font=8.3)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white),
                       ("BACKGROUND", (0, -1), (-1, -1), PALE2)]))
story.append(t)
story.append(small("i7-9750H 冷机，0.35 s × 3，单位见前表；多核分为单核参考下的缩放分。"))

# ---------------------------------------------------------------- cross arch
story.append(PageBreak())
story.append(section("07  跨架构：ARCH / NATIVE / SYSTEM"))
story.append(p("跨架构没有唯一公平总分。2.0 提供三种口径，分别回答三个问题。"))
rows = [
    ["口径", "是否使用 SIMD", "线程", "参考", "回答的问题"],
    ["ARCH", "否，纯标量", "1", "i7 标量 1000", "谁的标量架构 / 频率更强"],
    ["NATIVE", "是，原生宽度", "1", "i7 向量 1000", "同一原生 workload 谁更快"],
    ["SYSTEM", "是，原生宽度", "全部", "单核参考 × 倍率", "整机多线程吞吐"],
]
t = data_table(rows, [62, 100, 50, 115, 155], font=8.2)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)
story.append(Spacer(1, 10))
# cross bar
d = Drawing(CONTENT_W, 150)
labels = ["i7 ARCH", "M1 ARCH", "i7 1T native", "M1 1T native"]
vals = [998, 991, 1000, 893]
colors = [CYAN, GREEN, BLUE, HexColor("#7C3AED")]
for i, (lab, v) in enumerate(zip(labels, vals)):
    x = 45 + i * 105
    h = v / 1100 * 90
    d.add(Rect(x, 30, 56, h, fillColor=colors[i], strokeColor=colors[i]))
    txt(d, x + 28, 34 + h, f"{v}", 8, INK, "middle")
    txt(d, x + 28, 14, lab, 7, MUTED, "middle")
d.add(Line(40, 30, 470, 30, strokeColor=LINE))
txt(d, 0, 130, "ARCH 基本打平；NATIVE 单核仍受 128-bit NEON vs 256-bit AVX2 影响", 8, NAVY)
story.append(d)
story.append(h3("结论"))
for x in [
    "M1 的 ARCH 991 与 i7 的 998 基本打平：标量口径下没有明显劣势。",
    "M1 的 NATIVE 单核较低，主要来自 NEON 128 位对 AVX2 256 位的宽度差异，以及虚拟环境。",
    "NATIVE 多核不能跨架构比：3 vCPU M1 的 2.41× 不代表 8 核 M1。",
    "跨架构请只看 ARCH；NATIVE / SYSTEM 用于同架构或整机比较。",
]:
    story.append(bullet(x))
story.append(callout("为什么不让 x86 降成 128 位",
    "那会刻意抹掉 i7 的真实 AVX2 能力。正确做法不是阉割一方，而是提供 ARCH 标量口径和 NATIVE 原生口径，让用户明确知道自己在比较什么。", HexColor("#FFF7ED"), AMBER))
story.append(h2("ARCH 分项：i7 vs M1"))
rows = [
    ["项目", "i7 分数", "M1 分数", "观察"],
    ["INT64 add", "1061", "889", "i7 约高 19%"],
    ["INT64 mul", "1078", "1223", "M1 约高 13%"],
    ["INT64 hash", "1086", "648", "i7 约高 68%"],
    ["FP64 FMA", "1075", "690", "i7 约高 56%"],
    ["FP64 sqrt", "1090", "1962", "M1 约高 80%"],
    ["ARCH 总分", "1078", "991", "标量口径基本打平"],
]
t = data_table(rows, [100, 90, 90, 223], font=8.2)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white),
                       ("BACKGROUND", (0, -1), (-1, -1), PALE2)]))
story.append(t)
story.append(small("i7 为 2.0 实测；M1 为 3 vCPU GitHub Actions，单线程 ARCH 不受 vCPU 数影响，但仍受虚拟化/散热影响。分项差异说明 ARCH 不是“所有项目完全同速”，而是同一套标量 workload 下的真实架构差异。"))

# ---------------------------------------------------------------- memory
story.append(PageBreak())
story.append(section("08  Memory / Cache 模块"))
story.append(p("内存模块完全独立：它不进入 CPU 的 ARCH / NATIVE / SYSTEM 分，也不改变 CPU 参考常数。"))
story.append(h2("测什么"))
for x in [
    "<b>指针追逐延迟</b>：Sattolo 单环随机排列，避免短环；工作集覆盖 L1、L2、L3/LLC、DRAM。",
    "<b>顺序带宽</b>：Read、Write、Copy、Triad；默认 256 MB，避免落进 L3。",
    "<b>缓存大小</b>：macOS sysctl，Windows GetLogicalProcessorInformation，自动选择工作集。",
]:
    story.append(bullet(x))
d = Drawing(CONTENT_W, 175)
# latency log chart inline
labels = ["L1", "L2", "L3/LLC", "DRAM"]
vals = [1.84, 3.31, 54.2, 117.1]
txt(d, 0, 162, "指针追逐延迟（ns/access，对数刻度）", 9, NAVY)
import math
vmin, vmax = 1.0, 200.0
lo, hi = math.log10(vmin), math.log10(vmax)
for i, (lab, v) in enumerate(zip(labels, vals)):
    x = 50 + i * 100
    h = (math.log10(v) - lo) / (hi - lo) * 105
    d.add(Rect(x, 28, 54, h, fillColor=[CYAN, BLUE, HexColor("#7C3AED"), RED][i],
               strokeColor=None))
    txt(d, x + 27, 34 + h, f"{v:g}", 7.6, INK, "middle")
    txt(d, x + 27, 12, lab, 7.4, MUTED, "middle")
for t in [1, 10, 100]:
    y = 28 + (math.log10(t) - lo) / (hi - lo) * 105
    d.add(Line(40, y, 450, y, strokeColor=LINE, strokeWidth=0.4))
    txt(d, 8, y - 2, f"{t:g}", 6.8, MUTED)
story.append(d)
d2 = bar_chart(["Read", "Write", "Copy", "Triad"], [14.39, 11.40, 14.97, 16.30],
               "GB/s", [CYAN, BLUE, HexColor("#7C3AED"), GREEN],
               width=CONTENT_W, height=145, ymax=20,
               title="顺序带宽（i7-9750H，单线程，256 MB）")
story.append(d2)
story.append(small("i7-9750H 参考：L1 1.84 ns，L2 3.31 ns，L3/LLC 54.2 ns，DRAM 117.1 ns；Read 14.39，Write 11.40，Copy 14.97，Triad 16.30 GB/s。"))
story.append(callout("为什么独立",
    "CPU 计算分要回答“寄存器算术多快”；内存分要回答“数据搬运多快”。混在一起会让缓存大的 CPU 看起来 ALU 更强。2.0 让两者各自成表、各自 JSON，不做跨域总分。"))
story.append(h2("如何读 Memory 结果"))
for x in [
    "<b>L1/L2</b> 主要反映核心私有缓存和 TLB 命中；不同 P/E 核的 L1/L2 可能不同。",
    "<b>L3/LLC</b> 是共享缓存，受其它核心和系统缓存占用影响；8 MB 工作集不一定完全落在 L3。",
    "<b>DRAM</b> 包含 page walk 和内存控制器延迟；117 ns 是 i7-9750H 的典型值。",
    "<b>Copy/Triad</b> 计入读写总流量，所以数值看起来高于 Read；不要把它当成单向读带宽。",
    "<b>单线程</b>：本模块不测多核总带宽；多核带宽会受内存通道、NUMA 和功耗墙影响。",
]:
    story.append(bullet(x))

# ---------------------------------------------------------------- platforms
story.append(PageBreak())
story.append(section("09  平台与交付"))
story.append(h2("macOS"))
for x in [
    "Intel：AVX2 + FMA + 256 位向量 sqrt，另有通用 x86_64 回退。",
    "Apple Silicon：M1/M2/M3/M4 分别用 -mcpu 编译；128 位 NEON、16 条 fms 链、2-lane 向量 sqrt。",
    "app 自动选择二进制；不需要编译器。",
    "QoS 使用 USER_INTERACTIVE；可选 --pcores。",
]:
    story.append(bullet(x))
story.append(h2("Windows"))
for x in [
    "x86_64 AVX2 / 通用 x86_64 / ARM64 三个 exe。",
    "AVX2 4-lane vsqrtpd；全部逻辑核心。",
    "崩溃日志 + 错误弹窗 + AVX2 失败自动降级。",
    "Run-CPU-Bench.bat / Run-CPU-Bench-USB.bat。",
]:
    story.append(bullet(x))
story.append(h2("文件"))
rows = [
    ["文件", "作用"],
    ["Benchmark 2.0 Ultimate.app", "macOS CPU 主程序"],
    ["Benchmark-2.0-Ultimate-Windows.zip", "Windows CPU 主程序"],
    ["Benchmark Memory.app", "macOS 内存模块"],
    ["Benchmark-Memory-Windows.zip", "Windows 内存模块"],
    ["bench.c / windows/bench_win.c", "源码"],
    ["memory/memory_bench.c", "内存模块源码"],
]
t = data_table(rows, [245, 258], font=8.5)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)
story.append(Spacer(1, 8))
story.append(p("macOS 主程序会把资源复制到 ~/Library/Application Support/CPU性能跑分/；Memory 模块写到 ~/Library/Application Support/Benchmark Memory/。Windows 结果默认在 %USERPROFILE%\\CPU-Bench-Results。"))
story.append(h2("二进制选择"))
rows = [
    ["目标", "二进制", "ISA / 说明"],
    ["Intel Mac，AVX2+FMA", "cpu-bench-x86_64-avx2", "AVX2 256-bit，向量 sqrt"],
    ["较老 Intel Mac", "cpu-bench-x86_64-generic", "SSE2 回退，标量 sqrt"],
    ["Apple M1 / M2 / M3 / M4", "cpu-bench-arm64-m1..m4", "NEON 128-bit，16 链，2-lane sqrt"],
    ["未知 Apple Silicon", "cpu-bench-arm64", "通用 arm64 回退"],
    ["Windows x86_64", "cpu-bench-x86_64-avx2.exe", "AVX2 + 4-lane sqrt"],
    ["Windows 老 x86_64", "cpu-bench-x86_64-generic.exe", "通用回退"],
    ["Windows ARM64", "cpu-bench-arm64.exe", "ARM64 路径（非深度 NEON 优化）"],
]
t = data_table(rows, [130, 190, 183], font=8.1)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)
# ---------------------------------------------------------------- strengths
story.append(PageBreak())
story.append(section("10  优点"))
strengths = [
    ("纯计算，不被内存掩盖", "所有 CPU 热循环都在寄存器和局部变量中，避免大数组把带宽瓶颈伪装成 CPU 差异。"),
    ("多核缩放直观", "MULTI SCORE = SINGLE × MULTI SCALE，12 线程不会再显示成和单核差不多。"),
    ("跨架构有独立口径", "ARCH 使用标量、同运算量、单线程，M1 与 i7 可以放在同一把尺子上。"),
    ("原生宽度不被阉割", "x86 保留 AVX2 256 位，Apple Silicon 保留 NEON 128 位；NATIVE 分反映真实实现能力。"),
    ("防编译器优化", "noinline、asm 屏障、volatile sink、关闭整数向量化，避免循环被折叠或删除。"),
    ("平台适配完整", "macOS QoS、P/E 核、M1-M4 专用二进制；Windows 崩溃兜底、自动降级、U 盘模式。"),
    ("独立 Memory 模块", "缓存/DRAM 延迟和带宽单独输出，不污染 CPU 分。"),
    ("可复现输出", "固定参考、JSON、版本化字段、明确的运行条件。"),
]
for title, text in strengths:
    story.append(Paragraph(f"<b>{title}</b>", H3))
    story.append(p(text))
story.append(h2("适用场景"))
rows = [
    ["场景", "推荐口径", "说明"],
    ["M1 vs i7 架构讨论", "ARCH", "标量 1T，同指令工作量"],
    ["同架构 CPU 升级", "NATIVE / SYSTEM", "保留 AVX2/NEON 宽度差异"],
    ["整机多核能力", "SYSTEM + MULTI SCALE", "看核心数、P/E、功耗墙"],
    ["缓存/内存瓶颈", "Memory 模块", "独立于 CPU 分"],
    ["游戏/GPU 性能", "不适合", "本工具不测 GPU"],
]
t = data_table(rows, [130, 150, 223], font=8.2)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)

# ---------------------------------------------------------------- weaknesses
story.append(PageBreak())
story.append(section("11  不足与风险"))
weaknesses = [
    ("短时测试，不是稳态", "0.35 秒样本更接近 boost 爆发；9750H 冷机 1006，热机可能只有 900 左右。需要 --samples 5 或冷却后复跑。"),
    ("参考机固定", "1000 分来自 i7-9750H 冷机；不同编译器、OS、散热的同一 CPU 也会偏离。"),
    ("NATIVE 不适合跨架构", "AVX2 256 vs NEON 128 是实际宽度差异；M1 的 NATIVE 分低不代表整机弱。"),
    ("ARCH 仍含频率因素", "标量单线程仍受频率、功耗、编译器影响，不是纯 IPC。"),
    ("Windows 异构核未绑定", "目前不细化 P/E 拓扑，也不做线程亲和性。"),
    ("Windows ARM64 非深度优化", "有 ARM64 路径，但不是 Mac 的 16 链 NEON 版本。"),
    ("无 AVX-512 / SVE", "当前固定 AVX2/NEON 路径；支持更宽 ISA 的平台没有单独上限测试。"),
    ("内存模块是单线程", "带宽是单线程顺序带宽，不是多核总带宽，也不测 NUMA。"),
    ("权重是等权几何平均", "六个测试等权是设计选择，不是所有真实应用的权重。"),
    ("热降频与后台负载", "任何短时跑分都无法完全消除；只能通过中位数、冷却和固定条件减轻。"),
]
for title, text in weaknesses:
    story.append(Paragraph(f"<b>{title}</b>", H3))
    story.append(p(text))
story.append(h2("如何降低这些风险"))
rows = [
    ["风险", "操作建议"],
    ["热降频/boost", "冷却 5 分钟，关闭后台；关键比较用 --samples 5 或延长 duration"],
    ["参考机漂移", "同一版本、同一参考；记录 CPU 型号、电源和温度状态"],
    ["多核倍率误解", "同时看 MULTI SCALE 和 PHYS EFF，不把多核分当单核算力"],
    ["跨架构误读", "跨架构只看 ARCH；NATIVE 只做同架构比较"],
    ["内存混淆", "Memory 结果单独记录，不并入 CPU 总分"],
]
t = data_table(rows, [130, 373], font=8.3)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)

# ---------------------------------------------------------------- usage
story.append(PageBreak())
story.append(section("12  使用、复现与路线图"))
story.append(h2("CPU"))
story.append(p("macOS 双击 Benchmark 2.0 Ultimate.app；Windows 解压后双击 Run-CPU-Bench.bat。默认一次跑完 NATIVE 单核/多核和 ARCH。"))
rows = [
    ["参数", "作用"],
    ["--threads N", "多核线程数，默认全部逻辑核"],
    ["--samples N", "采样次数，默认 3"],
    ["--duration SEC", "每样本目标时长，默认 0.35"],
    ["--pcores", "Apple Silicon 只跑 P 核"],
    ["--quick", "0.12 秒/次，1 次采样"],
    ["--json PATH", "保存 JSON"],
]
t = data_table(rows, [150, 353], font=8.5)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)
story.append(h2("Memory"))
story.append(p("macOS 双击 Benchmark Memory.app；Windows 双击 Run-Memory-Bench.bat。支持 --quick、--samples N、--size MB。"))
story.append(h2("建议的对比方法"))
for x in [
    "跨架构：只比 ARCH；最好用相同的 Clang 版本和相同线程策略。",
    "同架构：比 NATIVE 单核和 SYSTEM 多核；记录冷却/电源状态。",
    "整机：多核分已经包含实测倍率；同时看 MULTI SCALE 和 PHYS EFF。",
    "内存：单独看延迟和带宽，不和 CPU 分混。",
]:
    story.append(bullet(x))
story.append(h2("路线图"))
rows = [
    ["优先级", "改进", "价值"],
    ["P0", "更多样本/持续模式", "降低热降频和boost偏差"],
    ["P1", "Windows P/E 识别与绑定", "让 13650HX/14900 的异构调度可解释"],
    ["P1", "同 Clang 交叉编译 ARCH", "减少编译器差异"],
    ["P2", "AVX-512 / SVE 运行时派发", "覆盖更宽 ISA 的真实上限"],
    ["P2", "多线程内存带宽", "补全 Memory 模块"],
    ["P3", "Linux 原生包", "服务器/工作站"],
]
t = data_table(rows, [55, 230, 218], font=8.3)
t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, 0), NAVY2),
                       ("TEXTCOLOR", (0, 0), (-1, 0), white)]))
story.append(t)
story.append(Spacer(1, 8))
story.append(callout("最终结论",
    "Benchmark 2.0 Ultimate 不是一个“万能总分”，而是一组可解释的尺子：ARCH 看跨架构标量能力，NATIVE 看原生向量实现，SYSTEM 看整机多核，Memory 看数据搬运。它的价值在于明确边界，而不是制造一个看起来精确的单一数字。"))

# ---------------------------------------------------------------- build
out = os.path.join(os.path.dirname(__file__), "Benchmark-2.0-Ultimate-技术白皮书.pdf")
doc = Whitepaper(out)
doc.build(story)
print("WROTE", out, os.path.getsize(out))
