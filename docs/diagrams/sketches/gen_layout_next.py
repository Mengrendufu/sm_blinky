#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Layout proposal after the midTran mockup: page 1 = system health.

Writes ./layout-next.html. Regenerate with: python3 gen_layout_next.py
The panel is drawn at its own size (240x320), the same convention the other
sketches in this directory use.
"""
import math

CSS = """
body{background:#eef1f4;color:#1b1f23;font-family:"Segoe UI",sans-serif;margin:24px}
h1{font-size:20px;margin:0 0 4px} h2{font-size:15px;margin:26px 0 10px;
 border-bottom:1px solid #ddd;padding-bottom:4px}
.panels{display:flex;gap:36px;flex-wrap:wrap;align-items:flex-start}
.panel{position:relative;width:240px;height:320px;background:#0A0E12;
 border:2px solid #222;border-radius:6px;overflow:hidden;flex:0 0 auto}
.panel .cap{position:absolute;left:-2px;top:-24px;color:#1b1f23;font-size:13px}
.sep{width:240px;text-align:center;color:#888;font-size:11px}
.box{position:absolute;border:1px dashed #555F68;box-sizing:border-box;
 color:#9AA3AC;font-size:11px;padding:2px 4px}
.solid{position:absolute;background:#0A0E12;border:1px solid #1E2A32;
 border-radius:8px;box-sizing:border-box}
.row{position:absolute;height:18px;font-size:11px;color:#c9d1d9;
 display:flex;align-items:center;gap:4px}
.track{display:inline-block;width:96px;height:9px;background:#1E2A32;
 border-radius:5px;overflow:hidden}
.fill{display:block;height:100%;background:#C6FF3D}
.pct{width:26px;text-align:right;color:#fff}
.raw{color:#9AA3AC}
.led{position:absolute;width:10px;height:10px;border-radius:50%;
 background:#00C853;box-shadow:0 0 6px #00C853}
.dot{position:absolute;border-radius:50%;background:#555F68}
.tagon{position:absolute;width:16px;height:16px;border-radius:50%;background:#FFD54F;
 color:#1b1f23;font-size:10px;text-align:center;line-height:16px;font-weight:600}
.tag{position:absolute;color:#1b1f23;background:#fff8e1;border:1px solid #FFD54F;
 font-size:10px;padding:1px 4px;border-radius:3px;max-width:190px}
table{border-collapse:collapse;font-size:13px}
th,td{border:1px solid #ccd2d8;padding:4px 8px;text-align:left;vertical-align:top}
code{background:#e8ecf0;padding:0 3px;border-radius:3px}
.note{background:#fff8e1;border-left:3px solid #FFD54F;padding:8px 10px;
 font-size:13px;margin:10px 0}
"""


def arc_paths(cx, cy, r, frac):
    """A 180 degree gauge: the track and the value arc, plus the value point."""
    x0, y0 = cx - r, cy
    th = math.radians(180.0 - frac * 180.0)
    x1, y1 = cx + r * math.cos(th), cy - r * math.sin(th)
    big = 1 if frac > 0.5 else 0
    track = 'M %.1f %.1f A %d %d 0 0 1 %.1f %.1f' % (x0, y0, r, r, cx + r, cy)
    val = 'M %.1f %.1f A %d %d 0 %d 1 %.1f %.1f' % (x0, y0, r, r, big, x1, y1)
    return track, val


def gauge(frac, pct_text, caption):
    track, val = arc_paths(120, 116, 76, frac)
    return ('<svg width="240" height="130" viewBox="0 0 240 130" '
            'style="position:absolute;left:0;top:26px">'
            '<path d="%s" stroke="#1B2830" stroke-width="12" fill="none"/>'
            '<path d="%s" stroke="#00C3BC" stroke-width="12" fill="none"/>'
            '</svg>'
            '<div style="position:absolute;left:0;top:96px;width:240px;'
            'text-align:center;color:#fff;font-size:14px">%s</div>'
            '<div style="position:absolute;left:0;top:114px;width:240px;'
            'text-align:center;color:#9AA3AC;font-size:11px">%s</div>'
            % (track, val, pct_text, caption))


def row(y, name, pct, raw, width=96, fill_hex="#C6FF3D"):
    return ('<div class="row" style="left:20px;top:%dpx">'
            '<span style="width:58px">%s</span>'
            '<span class="track" style="width:76px">'
            '<span class="fill" style="width:%d%%;'
            'background:%s"></span></span>'
            '<span class="pct">%d%%</span>'
            '<span class="raw">%s</span></div>'
            % (y, name, pct, fill_hex, pct, raw))


def tagon(x, y, n):
    return '<div class="tagon" style="left:%dpx;top:%dpx">%s</div>' % (x, y, n)


def tagbox(x, y, text):
    return '<div class="tag" style="left:%dpx;top:%dpx">%s</div>' % (x, y, text)


def page1():
    o = ['<div class="panel"><div class="cap">page 1 &middot; 系统健康（新）</div>']
    o.append('<div style="position:absolute;left:0;top:0;width:240px;height:24px;'
             'border-bottom:1px solid #1E2A32"></div>')
    o.append('<div style="position:absolute;left:8px;top:6px;color:#E0E4E8;'
             'font-size:14px">sys</div>')
    o.append('<div class="led" style="left:222px;top:7px"></div>')
    o.append('<div style="position:absolute;left:0;top:7px;width:214px;'
             'text-align:right;color:#9AA3AC;font-size:11px">cpu 38 %</div>')
    o.append(tagon(200, 4, "1"))
    o.append(gauge(0.38, "38 %", "cpu"))
    o.append(tagon(44, 44, "2"))
    o.append('<div class="solid" style="left:12px;top:170px;width:216px;height:126px">'
             '</div>')
    o.append(row(180, "ui", 36, "728 w"))
    o.append(row(198, "flash", 12, "212 w"))
    o.append(row(216, "blinky", 44, "902 w"))
    o.append(row(234, "nmin", 100, "16/16", fill_hex="#00C3BC"))
    o.append(row(252, "evt", 75, "12/16", fill_hex="#FFD54F"))
    o.append(tagon(14, 172, "3"))
    o.append('<div class="dot" style="left:112px;top:310px;width:6px;height:6px"></div>')
    o.append('<div class="dot" style="left:124px;top:311px;width:4px;height:4px"></div>')
    o.append(tagon(96, 296, "4"))
    o.append('</div>')
    return "".join(o)


def page2():
    o = ['<div class="panel"><div class="cap">page 2 &middot; live（原图为空，内容我提案）</div>']
    o.append('<div style="position:absolute;left:0;top:0;width:240px;height:24px;'
             'border-bottom:1px solid #1E2A32"></div>')
    o.append('<div style="position:absolute;left:8px;top:6px;color:#E0E4E8;'
             'font-size:14px">live</div>')
    o.append('<div class="led" style="left:222px;top:7px"></div>')
    o.append('<div style="position:absolute;left:0;top:7px;width:214px;'
             'text-align:right;color:#9AA3AC;font-size:11px">cpu 38 %</div>')
    o.append(tagon(200, 4, "5"))
    # spinner: a ring with a gap
    o.append('<svg width="96" height="96" viewBox="0 0 96 96" '
             'style="position:absolute;left:16px;top:40px">'
             '<circle cx="48" cy="48" r="30" stroke="#1B2830" stroke-width="8" '
             'fill="none"/>'
             '<path d="M 48 18 A 30 30 0 0 1 74 33" stroke="#00C3BC" '
             'stroke-width="8" fill="none" stroke-linecap="round"/></svg>')
    o.append('<div style="position:absolute;left:16px;top:138px;width:96px;'
             'text-align:center;color:#9AA3AC;font-size:10px">spinner</div>')
    o.append(tagon(104, 42, "6"))
    # switch: track + knob, driven by ledOn
    o.append('<div style="position:absolute;left:132px;top:56px;width:56px;'
             'height:28px;border-radius:14px;background:#00C3BC"></div>')
    o.append('<div style="position:absolute;left:162px;top:59px;width:22px;'
             'height:22px;border-radius:50%;background:#fff"></div>')
    o.append('<div style="position:absolute;left:132px;top:90px;width:88px;'
             'color:#9AA3AC;font-size:10px">switch &larr; ledOn</div>')
    o.append(tagon(190, 58, "7"))
    # cpu history: 12 bars, one per 500 ms window
    heights = [10, 14, 12, 18, 16, 22, 20, 26, 24, 30, 28, 32]
    for i, h in enumerate(heights):
        o.append('<div style="position:absolute;left:%dpx;top:%dpx;width:12px;'
                 'height:%dpx;background:#C6FF3D;border-radius:2px"></div>'
                 % (18 + i * 15, 226 - h, h))
    o.append('<div style="position:absolute;left:18px;top:230px;width:186px;'
             'color:#9AA3AC;font-size:10px">cpu 历史：每 500 ms 插一根（12 根 = 6 s）</div>')
    o.append(tagon(206, 200, "8"))
    o.append('<div class="dot" style="left:112px;top:311px;width:4px;height:4px"></div>')
    o.append('<div class="dot" style="left:124px;top:310px;width:6px;height:6px"></div>')
    o.append('</div>')
    return "".join(o)


HTML = """<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>sm_blinky 版面方案（下一版）</title>
<style>@CSS@</style></head><body>
<h1>sm_blinky 版面方案草图（按 midTran mockup）</h1>
<div style="font-size:13px;color:#555">源图：/<code>midTran/tmp_agent/sm_blinky_layout.png</code>
　本草图 = 我对那张图的理解 + 落到 240x320 与现有部件上的映射。</div>

<h2>一、我从图里读到的</h2>
<ul style="font-size:13px;line-height:1.7">
<li><b>页 1 = 系统健康页</b>：上部一大块写 <code>cpu占用率 弧形</code> &rarr; <b>cpu 占用率用弧表显示</b>（弧内带百分比数字）。</li>
<li>弧下面一个清单框，三项：<b>① FreeRTOS 栈占用率（各个线程）② SM_tracer 的 nmin ③ sm 事件池的空闲块</b>。</li>
<li>箭头批注：<b>“都用条状的百分比+数字来显示”</b> &rarr; 清单里每一项 = <b>一条 + 百分比 + 数字</b>（不是纯文字）。</li>
<li>右上角一个 <code>led灯</code> 框 &rarr; <b>led 在右上角</b>；<b>角落的 <code>cpu NN %</code> 文本保留</b> —— 你后来明确了「页 2 的右上角要有 cpu 占用率」，所以两页角落都留着它（页 1 的弧里另有一份）。</li>
<li>页 2 在图上<b>是空的</b> &rarr; 内容待你定（我给了推荐）。</li>
</ul>

<h2>二、落到 240x320 与现有薄封装上</h2>
<div class="panels">@P1@@P2@</div>
<div style="font-size:13px;line-height:1.8;margin-top:14px">
<b>①</b> 右上角：<code>cpu NN %</code> + led —— <b>两页都在</b>（页 1 的弧里也有一份，角落那个不取消 ✓）。<br>
<b>②</b> cpu 用<b>半圆</b> <code>lv_arc</code>：bbox 156x78 &rArr; 整行带 3 条 &asymp; 10 ms/次，2 Hz &rArr; 占用 ~2%。<br>
<b>③</b> 清单每行 = <b>条 + 百分比 + 数字</b>（mockup 原话）；这里示例 5 行：三个线程栈 + tracer nmin + 事件池空闲。<br>
<b>④</b> 页点保留（两页指示）。<br>
<b>⑤⑥⑦⑧</b> 页 2「随便动起来」：<code>spinner</code>（连续转，36x36 区域，&asymp;0.9 ms/帧 &rArr; 30 fps &asymp; 2%）+
 <code>switch</code>（跟 <code>ledOn</code> 同步翻转，每次 &asymp;2 ms、2 Hz &rArr; 0.4%）+
 <b>12 根 cpu 历史条</b>（每 500 ms 插一根，用现成的 <code>lv_bar</code>，每窗 &asymp;1.5 ms &rArr; 0.3%）+
 led 闪烁。合计 &asymp;3%，不碰大动画。<br>
&nbsp;&nbsp;&nbsp;&nbsp;<b>代价</b>：需要开 <code>LV_USE_SPINNER</code> 与 <code>LV_USE_SWITCH</code>（<code>bsp/lvgl/lv_conf.h</code> 现在是 0 ⇒ +约 3 KB flash）；
 <code>LV_USE_CHART</code> 建议<b>不开</b>（+6 KB 且走画线路径，历史条已经够用）。
</div>

<div class="note"><b>三、数据来源（都能拿到，且都便宜）</b>
<table>
<tr><th>显示</th><th>来源</th><th>成本 / 备注</th></tr>
<tr><td>cpu 半圆弧 + 弧内 %</td><td><code>BSP_cpuSample()</code> 差分（已有 ✓）</td>
<td>2 Hz 刷一次；弧 bbox 156x78 &rArr; 整行带 3 条 &asymp; 10 ms/次 &rArr; 占用 ~2% ✓</td></tr>
<tr><td>各线程栈占用（每行一条）</td>
<td><code>uxTaskGetSystemState()</code> + <code>usStackHighWaterMark</code>；
 本内核 <code>configUSE_TRACE_FACILITY=1</code> 已开 ✓</td>
<td>一次调用给全部任务（名字 + 水位）；任务有 ui / flash / blinky / Tmr Svc / IDLE &rArr;
 建议按占用排序取前 3~4 行，其余折成一行<br>注意：水位是<b>历史峰值</b>（只升不降）✓</td></tr>
<tr><td>tracer nmin</td><td><code>SM_Tracer_inst.nMin</code> / <code>nTot</code>(=16)</td>
<td>一个字节读；0 = 泵落后到丢记录 &rArr; 那一条建议变红/黄</td></tr>
<tr><td>事件池空闲块</td>
<td>待接：SST 静态事件池（<code>ports/sst_port.h: SST_EVT_POOL_NUM 1</code>）需要一个 free 读数；
 另有一个候选是 UI inbox（<code>ui_evt.c: UI_INBOX_LEN 16</code> + <code>Ui_used_</code>，现成 ✓）</td>
<td>一个计数读；接到后 1 行搞定</td></tr>
</table>
<div style="margin-top:6px">弧里那个数字用 <b>默认字体（14 px）</b>：放大曾被否
（<code>lv_transform</code> 会走 layer 路径、要 ARGB8888，本构建关掉了）&rArr;
真要大字号得做一份字体，不是一行代码的事。</div>
</div>

<h2>四、要你定的三件事</h2>
<ol style="font-size:13px;line-height:1.7">
<li><b>页 2 已定</b>：按你说的「随便放点组件 + 动效」= 上图 live 页（spinner + switch + 12 根 cpu 历史条 + led ✓，合计 &asymp;3% 占用）。</li>
<li><b>今天 blinky 的那套东西去哪</b>：节拍环（七步/边沿）、<code>edges/frame</code> 卡、底栏帧时条 ——
 新方案里<b>没有它们的位置</b>。选一个：<b>(a)</b> 搬到页 1 清单下面（要压掉 2 根条）；
 <b>(b)</b> 搬到页 2 换掉历史条；<b>(c)</b> 就删掉（板子上从此只看资源，不看 blinky 自己的节拍）。</li>
<li><b>栈要列哪几行</b>：全部任务一行一行（最多 5~6 行）还是按占用取前 N + 其余合计？</li>
<li><b>“sm 事件池”指哪个</b>：SST 静态事件池（字面 ✓，需加一个 free 读数）还是 UI inbox（现成 ✓）？</li>
</ol>
<div style="font-size:12px;color:#666">（这版是草稿：只画了几何与数据源；你点头后我把它并回
 <code>gen_pages_two.py</code> 并动 <code>sm_ui.c</code> / <code>widgets/w_*</code>。）</div>
</body></html>
"""

if __name__ == "__main__":
    html = HTML.replace("@CSS@", CSS).replace("@P1@", page1()).replace("@P2@", page2())
    with open("layout-next.html", "w", encoding="utf-8") as f:
        f.write(html)
    print("写出 ./layout-next.html  (%d bytes)" % len(html.encode("utf-8")))
