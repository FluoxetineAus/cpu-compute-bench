# -*- coding: utf-8 -*-
"""Generate the Benchmark 2.0 Application technical whitepaper."""
import os, math
from reportlab.lib.pagesizes import A4
from reportlab.lib.colors import HexColor, white
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph,
                                Spacer, PageBreak, Table, TableStyle,
                                NextPageTemplate)
from reportlab.graphics.shapes import Drawing, Rect, String, Line, Polygon

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
PURPLE = HexColor("#7C3AED")
LINE = HexColor("#D7DEE8")
PALE = HexColor("#F3F7FB")
PALE2 = HexColor("#E8F3F7")

PAGE_W, PAGE_H = A4
M = 46
CONTENT_W = PAGE_W - 2 * M

def st(name, size, leading=None, color=INK, align=TA_LEFT, sb=0, sa=6):
    return ParagraphStyle(name, fontName="CJK", fontSize=size,
                          leading=leading or size * 1.45, textColor=color,
                          alignment=align, spaceBefore=sb, spaceAfter=sa,
                          wordWrap="CJK")

BODY = st("body", 10.2, 16.0, INK, sa=7)
BODY_S = st("body_s", 8.9, 13.6, MUTED, sa=5)
BULLET = st("bullet", 10.0, 15.4, INK, sa=4)
H1 = st("h1", 21, 27, NAVY, sa=9)
H2 = st("h2", 14, 19, NAVY, sb=9, sa=6)
H3 = st("h3", 11.2, 15, CYAN, sb=6, sa=4)
CAP = st("cap", 8.1, 11, MUTED, sa=4)
COVER_H = st("cover_h", 31, 38, white, sa=9)
COVER_SUB = st("cover_sub", 12.6, 19, HexColor("#A9C4DA"), sa=10)
COVER_SMALL = st("cover_small", 9.0, 14, HexColor("#8FA9C0"))

def txt(d, x, y, s, size=9, color=INK, anchor="start"):
    t = String(x, y, s, fontName="CJK", fontSize=size, fillColor=color)
    if anchor == "middle": t.textAnchor = "middle"
    elif anchor == "end": t.textAnchor = "end"
    d.add(t)

def box(d, x, y, w, h, fill, stroke=None, r=7, title=None, sub=None,
        tc=white, sc=None, size=9.0):
    d.add(Rect(x, y, w, h, rx=r, ry=r, fillColor=fill,
               strokeColor=stroke or fill, strokeWidth=0.8))
    if title:
        txt(d, x + w/2, y + h/2 + (5 if sub else 0), title, size, tc, "middle")
    if sub:
        txt(d, x + w/2, y + h/2 - 9, sub, 7.6, sc or HexColor("#DCE8F3"), "middle")

def arrow(d, x1, y1, x2, y2, color=MUTED, width=1.4):
    d.add(Line(x1, y1, x2, y2, strokeColor=color, strokeWidth=width))
    ang = math.atan2(y2-y1, x2-x1); L = 6.5
    d.add(Polygon([x2, y2,
                   x2 - L*math.cos(ang-0.45), y2 - L*math.sin(ang-0.45),
                   x2 - L*math.cos(ang+0.45), y2 - L*math.sin(ang+0.45)],
                  fillColor=color, strokeColor=color))

def data_table(rows, widths, font=8.3, header=True):
    data = []
    for ri, r in enumerate(rows):
        color = white if (header and ri == 0) else INK
        data.append([Paragraph(str(c), st("cell", font, font*1.45, color,
                                          TA_LEFT, 0, 0)) for c in r])
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("GRID", (0,0), (-1,-1), 0.35, LINE),
             ("VALIGN", (0,0), (-1,-1), "MIDDLE"),
             ("LEFTPADDING", (0,0), (-1,-1), 5),
             ("RIGHTPADDING", (0,0), (-1,-1), 5),
             ("TOPPADDING", (0,0), (-1,-1), 5),
             ("BOTTOMPADDING", (0,0), (-1,-1), 5)]
    if header:
        style += [("BACKGROUND", (0,0), (-1,0), NAVY2)]
    t.setStyle(TableStyle(style))
    return t

def grouped_bars(labels, s1, s2, colors, unit="", width=CONTENT_W, height=185,
                 ymax=None, legend=("i7-9750H", "M1 virtual")):
    d = Drawing(width, height)
    left, right, top, bottom = 38, 12, 26, 34
    pw, ph = width-left-right, height-top-bottom
    vmax = ymax or max(max(s1), max(s2)) * 1.18
    for i in range(5):
        y = bottom + ph*i/4
        d.add(Line(left, y, left+pw, y, strokeColor=LINE, strokeWidth=0.45))
        txt(d, left-5, y-2.5, f"{vmax*i/4:.0f}", 6.6, MUTED, "end")
    n = len(labels); gw = pw/n
    for i, lab in enumerate(labels):
        x = left + gw*i + gw*0.16
        bw = gw*0.30
        h1 = ph*s1[i]/vmax; h2 = ph*s2[i]/vmax
        d.add(Rect(x, bottom, bw, max(1, h1), fillColor=colors[0], strokeColor=colors[0]))
        d.add(Rect(x+bw+2, bottom, bw, max(1, h2), fillColor=colors[1], strokeColor=colors[1]))
        txt(d, x+bw+1, bottom-14, lab, 6.3, MUTED, "middle")
    d.add(Rect(left, height-16, 8, 8, fillColor=colors[0], strokeColor=colors[0]))
    txt(d, left+11, height-15, legend[0], 6.8, MUTED)
    d.add(Rect(left+80, height-16, 8, 8, fillColor=colors[1], strokeColor=colors[1]))
    txt(d, left+93, height-15, legend[1], 6.8, MUTED)
    txt(d, 0, 3, unit, 6.8, MUTED)
    return d

def draw_cover(canvas, doc):
    canvas.saveState()
    canvas.setFillColor(NAVY); canvas.rect(0, 0, PAGE_W, PAGE_H, fill=1, stroke=0)
    canvas.setStrokeColor(HexColor("#17304A")); canvas.setLineWidth(0.6)
    for y in (150, 190, 650, 690): canvas.line(0, y, PAGE_W, y)
    for x in (80, 110, 500, 530): canvas.line(x, 0, x, PAGE_H)
    canvas.setFillColor(CYAN); canvas.rect(M, 90, 92, 4, fill=1, stroke=0)
    canvas.setFillColor(HexColor("#0F2236")); canvas.circle(PAGE_W-95, PAGE_H-120, 52, fill=1, stroke=0)
    canvas.setFillColor(CYAN); canvas.circle(PAGE_W-95, PAGE_H-120, 7, fill=1, stroke=0)
    canvas.restoreState()

def draw_page(canvas, doc):
    canvas.saveState()
    canvas.setFont("CJK", 8); canvas.setFillColor(MUTED)
    canvas.drawString(M, PAGE_H-28, "Benchmark 2.0 Application  ·  技术白皮书")
    canvas.drawRightString(PAGE_W-M, PAGE_H-28, "2026-11-22")
    canvas.setStrokeColor(LINE); canvas.setLineWidth(0.5)
    canvas.line(M, PAGE_H-34, PAGE_W-M, PAGE_H-34)
    canvas.line(M, 30, PAGE_W-M, 30)
    canvas.drawString(M, 18, "Application · single + multi · macOS / Windows")
    canvas.drawRightString(PAGE_W-M, 18, f"{doc.page}")
    canvas.restoreState()

class Doc(BaseDocTemplate):
    def __init__(self, filename):
        BaseDocTemplate.__init__(self, filename, pagesize=A4,
            leftMargin=M, rightMargin=M, topMargin=52, bottomMargin=48,
            title="Benchmark 2.0 Application 技术白皮书",
            author="Benchmark 2.0 Application")
        cover = PageTemplate(id="cover", frames=[Frame(M, 40, CONTENT_W, PAGE_H-80)],
                             onPage=draw_cover)
        body = PageTemplate(id="body", frames=[Frame(M, 44, CONTENT_W, PAGE_H-92)],
                            onPage=draw_page)
        self.addPageTemplates([cover, body])

def p(text): return Paragraph(text, BODY)
def small(text): return Paragraph(text, BODY_S)
def bullet(text): return Paragraph(text, BULLET, bulletText="•")
def h2(text): return Paragraph(text, H2)
def h3(text): return Paragraph(text, H3)
def callout(title, text, color=PALE2, border=CYAN):
    t = Table([[Paragraph(f"<b>{title}</b><br/>{text}", BODY)]], colWidths=[CONTENT_W])
    t.setStyle(TableStyle([("BACKGROUND", (0,0), (-1,-1), color),
                           ("BOX", (0,0), (-1,-1), 0.7, border),
                           ("LEFTPADDING", (0,0), (-1,-1), 11),
                           ("RIGHTPADDING", (0,0), (-1,-1), 11),
                           ("TOPPADDING", (0,0), (-1,-1), 9),
                           ("BOTTOMPADDING", (0,0), (-1,-1), 9)]))
    return t

story = []

# ---------------------------------------------------------------- cover
story.append(Spacer(1, 52))
story.append(Paragraph("BENCHMARK APPLICATION", st("k", 11, 17, CYAN, sa=4)))
story.append(Paragraph("2.0", COVER_H))
story.append(Paragraph("应用型 CPU 基准 · 技术白皮书", COVER_SUB))
story.append(Paragraph("单线程 + 多线程 · 8 个真实任务 · macOS / Windows",
                       COVER_SMALL))
d = Drawing(450, 150)
d.add(Rect(0, 0, 450, 150, rx=14, ry=14, fillColor=HexColor("#0F2236"),
           strokeColor=HexColor("#1E3A52"), strokeWidth=0.8))
tasks_short = ["SHA", "LZSS", "JSON", "TEXT", "IMG", "SORT", "MAT", "COPY"]
for i, s in enumerate(tasks_short):
    x = 22 + i*52
    d.add(Rect(x, 38, 38, 64, rx=5, ry=5,
               fillColor=[CYAN, BLUE, PURPLE, GREEN, AMBER, RED, HexColor("#0E7490"), HexColor("#475569")][i],
               strokeColor=None))
    txt(d, x+19, 53, s, 6.8, white, "middle")
    txt(d, x+19, 31, "1T+MT", 5.8, HexColor("#9FB6CA"), "middle")
story.append(d)
story.append(Spacer(1, 22))
story.append(Paragraph("SINGLE  ·  MULTI  ·  SCALE  ·  OVERALL", st("tags", 9, 14,
                       HexColor("#9FB6CA"), sa=6)))
story.append(Paragraph("2026-11-22", COVER_SMALL))
story.append(NextPageTemplate("body"))
story.append(PageBreak())

# ---------------------------------------------------------------- 1 summary
story.append(Paragraph("01  摘要与关键结论", H1))
story.append(p("Benchmark 2.0 Application 是一个 Geekbench 风格的应用型基准：不测单个 FMA 微内核的理论峰值，而是让机器运行 8 个有代表性的应用任务，再把它们的完成速度组合成一个分数。"))
story.append(callout("一句话结论",
    "它回答的是“同一套应用任务，整机跑得多快”，不是“谁的 AVX2/NEON 峰值更高”。单线程可跨架构看应用性能；多线程默认全核是整机性能，不是架构分。"))
rows = [
    ["指标", "i7-9750H (12T)", "M1 virtual (3 vCPU)", "说明"],
    ["SINGLE APP", "953（冷机约 995）", "1328", "应用组合单线程"],
    ["MULTI APP", "3892", "3550", "8 任务多线程"],
    ["MULTI SCALE", "4.09x", "2.67x", "实测多核倍率"],
    ["OVERALL", "1925", "2172", "sqrt(SINGLE × MULTI)"],
]
t = data_table(rows, [95, 120, 130, 158], font=8.2)
t.setStyle(TableStyle([("BACKGROUND", (0,1), (-1,1), PALE2)]))
story.append(t)
story.append(Spacer(1, 7))
story.append(small("i7 为 12 线程热机示例；M1 为 GitHub Actions 3 vCPU，不是 8 核本机。分数只表示相对 i7-9750H 应用参考的速度。"))
story.append(h2("2.0 Application 的关键点"))
for x in [
    "<b>8 个任务</b>：SHA-256、LZSS、JSON、文本搜索、图像模糊、堆排序、矩阵乘法、内存拷贝。",
    "<b>统一基线</b>：i7-9750H = 1000，没有 ARM 单独基线，没有按架构加权。",
    "<b>单线程 + 多线程</b>：多核分 = 单核分 × 实测缩放，不做核心数乘法。",
    "<b>跨架构</b>：SINGLE APP 可用于 x86 / ARM 应用层比较；MULTI 默认全核是整机比较。",
]:
    story.append(bullet(x))

# ---------------------------------------------------------------- 2 boundaries
story.append(PageBreak())
story.append(Paragraph("02  设计目标与边界", H1))
story.append(h2("目标"))
for x in [
    "用多个真实任务，而不是单一微内核，降低某个 ISA 宽度对总分的支配。",
    "同一份 C 源码、同一套算法，在 macOS 和 Windows 原生编译。",
    "单线程和多线程都测，且把缩放单独展示。",
    "使用统一基线，避免“给某个架构换一把尺子”的伪公平。",
    "输出每项原始吞吐和分项分，便于解释总分。",
]:
    story.append(bullet(x))
story.append(h2("非目标"))
for x in [
    "不是纯 CPU IPC，也不是峰值 FLOPS。",
    "不测 GPU、NPU、磁盘、网络。",
    "不手写 AVX2/NEON 微内核；向量化交给编译器。",
    "不承诺和 Geekbench 分数直接可比：workload 组成和权重不同。",
]:
    story.append(bullet(x))
story.append(h2("和 CPU Compute / Geekbench 的关系"))
rows = [
    ["", "CPU Compute 2.0", "Application 2.0", "Geekbench 6"],
    ["题目", "6 个寄存器微内核", "8 个应用任务", "大量应用任务"],
    ["向量", "手写 AVX2/NEON", "编译器自动", "平台原生编译"],
    ["内存", "不测", "解析/图像/拷贝", "包含内存"],
    ["分数", "ARCH/NATIVE/SYSTEM", "SINGLE/MULTI/OVERALL", "一个综合分"],
    ["跨架构", "ARCH 标量", "SINGLE APP", "统一基线"],
]
t = data_table(rows, [55, 125, 125, 198], font=7.9)
story.append(t)
story.append(callout("为什么应用型也要保留分项",
    "应用型综合分容易变成黑盒。保留 8 项吞吐、单核/多核分和缩放，才能看出“为什么高”或“为什么低”，而不是只看到一个总分。", HexColor("#FFF7ED"), AMBER))

# ---------------------------------------------------------------- 3 architecture
story.append(PageBreak())
story.append(Paragraph("03  总体架构与线程模型", H1))
story.append(p("程序是单文件 C。主线程准备固定数据，然后分别跑单线程与多线程；多线程使用启动门闩，线程创建和销毁不计入计时区间。"))
d = Drawing(CONTENT_W, 155)
y = 88
box(d, 0, y, 82, 42, HexColor("#0F766E"), None, 7, "数据准备", "固定输入", size=8.4)
box(d, 94, y, 82, 42, HexColor("#0E7490"), None, 7, "单线程", "8 任务", size=8.4)
box(d, 188, y, 82, 42, HexColor("#1D4ED8"), None, 7, "多线程", "启动门闩", size=8.4)
box(d, 282, y, 82, 42, HexColor("#6D28D9"), None, 7, "评分", "单核/多核", size=8.4)
box(d, 376, y, 127, 42, HexColor("#B45309"), None, 7, "输出", "表格 + JSON", size=8.4)
for x in [82, 176, 270, 364]: arrow(d, x+4, y+21, x+12, y+21)
box(d, 55, 22, 170, 34, PALE2, HexColor("#BAE6FD"), 6, "独立工作区", "每线程输出/排序/矩阵缓冲", tc=NAVY, sc=MUTED, size=8)
box(d, 275, 22, 170, 34, PALE2, HexColor("#BAE6FD"), 6, "统一基线", "i7-9750H = 1000", tc=NAVY, sc=MUTED, size=8)
story.append(d)
story.append(h2("线程模型"))
for x in [
    "<b>每个线程一份完整任务</b>：SHA/JSON/文本只读共享输入；LZSS、图像、排序、矩阵、拷贝使用 per-thread 输出缓冲。",
    "<b>无数据竞争</b>：不共享可写输出；线程只写自己的 ctx。",
    "<b>启动门闩</b>：主线程创建全部线程，等它们自旋就绪后再开始计时；线程启动不算入任务时间。",
    "<b>线程数</b>：默认全部逻辑核，最多 24；也可 --threads N。",
]:
    story.append(bullet(x))
story.append(h2("模块边界"))
rows = [
    ["模块", "测量对象", "是否跨架构"],
    ["CPU Compute", "寄存器算术", "ARCH 可跨架构"],
    ["Application", "8 个应用任务", "SINGLE APP 可跨架构"],
    ["Memory / Cache", "缓存/DRAM 延迟与带宽", "独立工具"],
    ["GPU / NPU", "不测", "不在本工具范围"],
]
t = data_table(rows, [110, 220, 173], font=8.1)
story.append(t)

# ---------------------------------------------------------------- 4 tasks
story.append(PageBreak())
story.append(Paragraph("04  八个应用任务", H1))
rows = [
    ["任务", "类别", "输入/规模", "单位"],
    ["SHA-256", "Crypto", "8 MB 数据", "MB/s"],
    ["LZSS 压缩+解压", "Compression", "4 MB 输入", "MB/s"],
    ["JSON 解析", "Parsing", "约 18k 对象", "MB/s"],
    ["文本搜索", "Text", "8 MB 文本，16 模式", "MB/s"],
    ["图像模糊 5×5", "Imaging", "1024×1024 RGB", "MPix/s"],
    ["堆排序", "Sorting", "1,048,576 个 u32", "Melem/s"],
    ["矩阵乘法", "Numeric", "256×256 double", "GFLOP/s"],
    ["内存拷贝", "Memory", "16 MB/线程", "GB/s"],
]
t = data_table(rows, [122, 100, 180, 101], font=8.1)
story.append(t)
story.append(Spacer(1, 8))
story.append(h2("为什么是这些任务"))
for x in [
    "<b>SHA-256</b>：真实加密哈希，串行依赖强，考察整数/移位/乘法混合。",
    "<b>LZSS</b>：真实压缩/解压，考察分支、哈希表、内存访问和压缩率。",
    "<b>JSON</b>：真实解析器，考察字符处理、递归、分支和缓存。",
    "<b>文本搜索</b>：Boyer-Moore-Horspool，考察跳表和内存扫描；不用 libc strstr，避免平台差异。",
    "<b>图像模糊</b>：5×5 卷积，考察标量/自动向量化和邻域访问。",
    "<b>堆排序</b>：自己的 heapsort，避免 qsort 的平台差异。",
    "<b>矩阵乘法</b>：256³ 双精度，考察 FMA/自动向量化和循环。",
    "<b>内存拷贝</b>：自己的 64 位拷贝循环，考察单线程带宽，也是多核瓶颈项。",
]:
    story.append(bullet(x))
story.append(callout("没有手写 SIMD",
    "Application 不写 AVX2/NEON intrinsic。编译器可以使用它认为合适的向量化；这更接近应用代码，也避免把某个 ISA 的宽度直接写进 benchmark。"))

# ---------------------------------------------------------------- 5 single
story.append(PageBreak())
story.append(Paragraph("05  单线程评分", H1))
story.append(p("每个任务先自动校准到约 0.2 秒，再取 3 次中位数。单项分以 i7-9750H 的单线程吞吐为 1000。"))
for x in [
    "<b>单项分</b> = 1000 × 单核实测吞吐 / i7 单核参考吞吐",
    "<b>SINGLE APP</b> = 8 项单项分的几何平均",
    "<b>统一基线</b>：i7-9750H = 1000；没有 ARM 单独基线。",
]:
    story.append(bullet(x))
d = Drawing(CONTENT_W, 116)
box(d, 0, 56, 140, 40, HexColor("#0E7490"), None, 7, "8 个任务", "单线程吞吐", size=8.5)
box(d, 170, 56, 140, 40, HexColor("#1D4ED8"), None, 7, "8 个单项分", "除以 i7 参考", size=8.5)
box(d, 340, 56, 150, 40, HexColor("#6D28D9"), None, 7, "SINGLE APP", "几何平均", size=8.5)
arrow(d, 142, 76, 168, 76); arrow(d, 312, 76, 338, 76)
story.append(d)
story.append(h2("i7-9750H 单线程参考"))
rows = [
    ["任务", "参考吞吐", "单位"],
    ["SHA-256", "242.0", "MB/s"],
    ["LZSS", "75.9", "MB/s"],
    ["JSON", "686.6", "MB/s"],
    ["文本搜索", "1592.2", "MB/s"],
    ["图像模糊", "43.87", "MPix/s"],
    ["堆排序", "8.28", "Melem/s"],
    ["矩阵乘法", "7.96", "GFLOP/s"],
    ["内存拷贝", "13.64", "GB/s"],
]
t = data_table(rows, [170, 170, 163], font=8.2)
story.append(t)
story.append(small("参考值来自 i7-9750H 单线程、3 次采样；热机会低几个百分点。冷机单线程总分约 995，热机示例约 953。"))

# ---------------------------------------------------------------- 6 multi
story.append(PageBreak())
story.append(Paragraph("06  多线程评分与缩放", H1))
story.append(p("多线程不是“单核分 × 核心数”。每个任务实际跑 T 个线程，测 wall time，再用总工作量计算吞吐。多核分使用同一个单核参考，所以能直接看出相对单核的缩放。"))
for x in [
    "<b>单项多核分</b> = 1000 × 多核实测吞吐 / 同一单核参考",
    "<b>MULTI APP</b> = 8 项多核分的几何平均",
    "<b>MULTI SCALE</b> = 8 项（多核吞吐 / 单核吞吐）的几何平均",
    "<b>MULTI APP ≈ SINGLE APP × MULTI SCALE</b>",
    "<b>OVERALL</b> = sqrt(SINGLE APP × MULTI APP)",
]:
    story.append(bullet(x))
rows = [
    ["任务", "单核分", "12T 多核分", "缩放"],
    ["SHA-256", "930", "4782", "5.1x"],
    ["LZSS", "860", "4032", "4.7x"],
    ["JSON", "998", "4470", "4.5x"],
    ["文本搜索", "956", "9279", "9.7x"],
    ["图像模糊", "932", "4174", "4.5x"],
    ["堆排序", "1010", "3259", "3.2x"],
    ["矩阵乘法", "943", "4566", "4.8x"],
    ["内存拷贝", "1001", "1059", "1.1x"],
    ["总计", "953", "3892", "4.09x"],
]
t = data_table(rows, [105, 85, 115, 80], font=8.1)
t.setStyle(TableStyle([("BACKGROUND", (0,-1), (-1,-1), PALE2)]))
story.append(t)
story.append(h2("为什么只有 4.09x"))
for x in [
    "<b>内存拷贝饱和</b>：单线程已经接近 13.6 GB/s，12 线程只有 1.1x；它把几何平均拉低。",
    "<b>SMT 不是物理核</b>：12 线程 = 6 核 + SMT，排序/哈希/压缩的缩放低于 6x。",
    "<b>共享缓存/带宽</b>：图像、矩阵、解析都受 L3 和内存带宽影响。",
    "<b>文本搜索 9.7x</b>：数据小、缓存命中高，是特例，不代表所有任务。",
]:
    story.append(bullet(x))
story.append(callout("多核分的正确读法",
    "MULTI APP 是整机多线程吞吐，不是架构分。比较 M1 8C 和 i7 12T 的默认 MULTI APP，等于比较核心数、SMT、功耗墙和缓存，而不是单核架构。"))

# ---------------------------------------------------------------- 7 cross arch
story.append(PageBreak())
story.append(Paragraph("07  跨架构：能比什么，不能比什么", H1))
rows = [
    ["指标", "跨架构", "说明"],
    ["SINGLE APP", "可以", "单线程、统一基线、8 任务混合"],
    ["MULTI APP 默认全核", "不建议", "核心数/线程数不同，是整机分"],
    ["MULTI APP 固定线程", "近似", "两边 --threads 4 或 8 才有意义"],
    ["MULTI SCALE", "可参考", "看缩放效率，但核心数仍不同"],
    ["OVERALL 默认全核", "不建议", "混合了核心数和单核"],
]
t = data_table(rows, [145, 95, 263], font=8.2)
story.append(t)
story.append(Spacer(1, 8))
d = grouped_bars(["SINGLE", "MULTI", "SCALE×1000", "OVERALL"],
                 [953, 3892, 4090, 1925], [1328, 3550, 2670, 2172],
                 (BLUE, GREEN), unit="score / x1000 for SCALE",
                 height=175, ymax=4500, legend=("i7 12T", "M1 3vCPU"))
story.append(d)
story.append(h2("结论"))
for x in [
    "<b>SINGLE APP 可以跨架构</b>：M1 1328 vs i7 953/995，应用层单线程 M1 更强。",
    "M1 在 LZSS、图像、矩阵、排序、内存拷贝上强；i7 在 SHA-256、文本搜索上强。",
    "<b>MULTI APP 默认全核不能当架构分</b>：12T i7 vs 3 vCPU M1 是整机比较。",
    "固定线程数也只能近似：i7 有 SMT，M1 有 P/E 异构核。",
]:
    story.append(bullet(x))
story.append(callout("Application 的定位",
    "它比 CPU Compute 的 NATIVE 更适合跨架构，因为 workload 混合且不手写 SIMD；但它仍然是应用性能，不是 IPC，也不是峰值 FLOPS。"))

# ---------------------------------------------------------------- 8 measurement
story.append(PageBreak())
story.append(Paragraph("08  测量方法与线程安全", H1))
story.append(h2("校准与采样"))
for x in [
    "每个任务从 reps=1 开始，按耗时缩放到约 0.2 秒；再跑 3 次取中位数。",
    "单线程和多线程分别校准；多线程的 reps 是每线程的重复次数。",
    "多线程总操作数 = 每轮操作 × reps × 线程数。",
    "计时区间只包含任务；线程创建、门闩等待、输出/结果合并都在计时之外。",
]:
    story.append(bullet(x))
story.append(h2("线程安全"))
for x in [
    "SHA-256 / JSON / 文本：共享只读输入，线程只写局部变量。",
    "LZSS：每线程独立的 state、压缩输出、解压输出。",
    "图像：每线程独立的输出图像；输入共享只读。",
    "排序：每线程独立的 work 数组；原始数组只读。",
    "矩阵：每线程独立的 C 矩阵；A/B 只读。",
    "拷贝：每线程独立的目标缓冲；源缓冲只读。",
]:
    story.append(bullet(x))
story.append(h2("误差来源"))
rows = [
    ["来源", "影响", "控制"],
    ["频率/Boost/热", "5~20%", "预热、中位数、冷却后复跑"],
    ["后台负载", "1~10%", "关闭后台；固定电源模式"],
    ["内存带宽/缓存", "可达 20%+", "报告分项；不要只看总分"],
    ["线程调度", "多核可达 20%", "启动门闩；固定线程数；P/E 影响"],
    ["编译器", "数个百分点", "同一份 C；不手写 SIMD"],
]
t = data_table(rows, [115, 110, 278], font=8.0)
story.append(t)

# ---------------------------------------------------------------- 9 data
story.append(PageBreak())
story.append(Paragraph("09  实测数据：i7 vs M1", H1))
story.append(p("下面是单线程分项对比。M1 为 3 vCPU 虚拟环境，单线程不受 vCPU 数限制，但仍受虚拟化/散热影响。"))
d = grouped_bars(["SHA", "LZSS", "JSON", "TEXT", "IMG", "SORT", "MAT", "COPY"],
                 [930, 860, 998, 956, 932, 1010, 943, 1001],
                 [845, 1310, 895, 806, 1326, 1773, 1521, 3395],
                 (BLUE, GREEN), unit="single-task score (i7 reference = 1000)",
                 height=195, ymax=3800, legend=("i7", "M1 3vCPU"))
story.append(d)
story.append(h2("观察"))
for x in [
    "<b>M1 强项</b>：LZSS 1310、图像 1326、排序 1773、矩阵 1521、拷贝 3395。",
    "<b>i7 强项</b>：SHA 930 vs 845、文本 956 vs 806；x86 在串行整数/文本上仍有优势。",
    "<b>SINGLE APP</b>：M1 1328 高于 i7 953/995，是应用组合的结果，不是单个任务全胜。",
    "<b>多核</b>：M1 3 vCPU 2.67x；i7 12T 4.09x；核心数和线程数不同，不能直接比架构。",
]:
    story.append(bullet(x))
story.append(callout("M1 8 核推测",
    "按 M1 单线程约 1300、8 核缩放 5.0~5.5x 估算，M1 8 核 MULTI APP 约 6000~7000，OVERALL 约 2800~3050。实际受 Air/Pro 散热和 P/E 调度影响。", HexColor("#FFF7ED"), AMBER))

# ---------------------------------------------------------------- 10 strengths
story.append(PageBreak())
story.append(Paragraph("10  优点", H1))
items = [
    ("多任务摊薄 ISA 宽度", "8 个任务混合，不再由 AVX2 256 vs NEON 128 单独决定胜负。"),
    ("统一基线", "i7-9750H = 1000；没有 ARM 单独基线，避免伪公平。"),
    ("单核 + 多核", "SINGLE / MULTI / SCALE / OVERALL 分开，多核分体现真实缩放。"),
    ("不手写 SIMD", "应用代码交给编译器；macOS/Windows 同一份 C。"),
    ("减少 libc 差异", "文本 BMH、堆排序、拷贝循环都是自己的 C 实现。"),
    ("线程安全设计", "每线程独立输出；没有共享可写缓冲。"),
    ("可解释", "每项原始吞吐和分项分都输出，JSON 可复现。"),
    ("跨架构可用", "SINGLE APP 可以做 x86/ARM 应用层比较；固定线程数可做多核近似比较。"),
]
for title, text in items:
    story.append(Paragraph(f"<b>{title}</b>", H3))
    story.append(p(text))
story.append(h2("适用场景"))
rows = [
    ["场景", "推荐指标"],
    ["M1 vs i7 应用性能", "SINGLE APP"],
    ["同架构多核", "MULTI APP"],
    ["架构缩放效率", "MULTI SCALE + 固定线程"],
    ["整机应用性能", "MULTI APP 默认全核"],
    ["纯 CPU 架构", "CPU Compute ARCH"],
]
t = data_table(rows, [220, 283], font=8.2)
story.append(t)

# ---------------------------------------------------------------- 11 weaknesses
story.append(PageBreak())
story.append(Paragraph("11  不足与风险", H1))
items = [
    ("不是纯 CPU IPC", "包含内存、解析、排序、图像；不能回答纯微架构问题。"),
    ("内存拷贝拖低多核", "单线程已接近带宽上限，12T 只有 1.1x，几何平均被拉低。"),
    ("统一基线仍是 i7", "1000 分来自 i7；其他平台是客场，但这是设计选择。"),
    ("编译器差异", "macOS Clang vs Windows GCC/MinGW；同一份 C 仍有代码生成差异。"),
    ("线程数不同", "M1 8C 与 i7 12T 默认全核不能直接比架构。"),
    ("P/E 与 SMT", "M1 有 4P+4E；i7 有 SMT；固定线程也不是完全同构。"),
    ("热降频", "短时 boost 与持续性能不同；Air 和 Pro 差异明显。"),
    ("任务权重等权", "8 项几何平均是设计选择，不是真实应用权重。"),
]
for title, text in items:
    story.append(Paragraph(f"<b>{title}</b>", H3))
    story.append(p(text))
story.append(h2("降低风险"))
rows = [
    ["问题", "做法"],
    ["热降频", "冷却后复跑；记录 --samples 5；看单核分是否稳定"],
    ["多核误读", "跨架构用 SINGLE APP；固定线程才比 MULTI"],
    ["内存瓶颈", "同时看 MULTI SCALE 和 COPY 分项"],
    ["编译器差异", "同版本、同参数；不要跨版本比"],
]
t = data_table(rows, [120, 383], font=8.1)
story.append(t)

# ---------------------------------------------------------------- 12 usage
story.append(PageBreak())
story.append(Paragraph("12  使用、交付与路线图", H1))
story.append(h2("运行"))
for x in [
    "macOS：双击 Benchmark Application 2.0.app。",
    "Windows：解压 Benchmark-Application-2.0-Windows.zip，双击 Run-Application-Bench.bat。",
    "命令行：application-bench --threads N --samples N --json result.json。",
    "默认全部逻辑核心；--threads 1 只跑单线程。",
]:
    story.append(bullet(x))
rows = [
    ["参数", "作用"],
    ["--threads N", "多线程数，默认全部逻辑核，最多 24"],
    ["--samples N", "采样次数，默认 3"],
    ["--quick", "1 次采样，快速检查"],
    ["--json PATH", "保存完整分项 JSON"],
]
t = data_table(rows, [130, 373], font=8.2)
story.append(t)
story.append(h2("交付文件"))
rows = [
    ["文件", "用途"],
    ["Benchmark Application 2.0.app", "macOS 应用"],
    ["Benchmark-Application-2.0.zip", "macOS 压缩包"],
    ["Benchmark-Application-2.0-Windows.zip", "Windows 应用"],
    ["application/application_bench.c", "源码"],
]
t = data_table(rows, [240, 263], font=8.2)
story.append(t)
story.append(h2("路线图"))
rows = [
    ["优先级", "改进", "价值"],
    ["P1", "固定线程对比模式", "跨架构多核更可比"],
    ["P1", "P/E 绑定与报告", "M1 异构调度更清楚"],
    ["P2", "更多应用任务", "降低单任务权重"],
    ["P2", "持续负载模式", "看稳态而非 boost"],
    ["P3", "Linux 包", "服务器/工作站"],
]
t = data_table(rows, [60, 220, 223], font=8.1)
story.append(t)
story.append(Spacer(1, 8))
story.append(callout("最终结论",
    "Application 2.0 是一把“应用组合尺”：SINGLE APP 可以跨架构，MULTI APP 默认全核是整机，固定线程数才是多核近似。它不替代 CPU Compute 的 ARCH，也不冒充纯 IPC；它的价值是把真实任务的完成速度、单核和多核缩放同时摆出来。"))

out = os.path.join(os.path.dirname(__file__), "Benchmark-Application-2.0-技术白皮书.pdf")
doc = Doc(out)
doc.build(story)
print("WROTE", out, os.path.getsize(out))
