# -*- coding: utf-8 -*-
"""两页布局草图 v1：仪表页(bg-a) + 状态页(bg-b)，只用 LVGL 现成部件 + 背景图。

坐标全部按 gen_bg.py 里那套锚点（顶栏 y0-24 / 大环 (40,28)-(200,188) /
卡片 A(12,204)-(116,296) / 卡片 B(124,204)-(228,296) / 底栏 (16,304) 与 (176,304)），
所以两页共用同一套背景图锚点，换背景不换布局。
"""
S = 2                      # 放大倍数（屏幕 240x320 -> 480x640）
W, H = 240, 320

CSS = """
 body{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#f5f5f5;color:#222;margin:0;padding:24px 32px 64px}
 h1{font-size:20px;margin:0 0 4px} h2{font-size:15px;margin:26px 0 10px;border-bottom:1px solid #ddd;padding-bottom:4px}
 .sub{color:#666;font-size:13px;margin-bottom:18px;line-height:1.8}
 .row{display:flex;gap:26px;flex-wrap:wrap;align-items:flex-start}
 .frame{text-align:center}
 .screen{position:relative;width:480px;height:640px;border:1px solid #999;box-shadow:0 2px 10px rgba(0,0,0,.2);overflow:hidden;background:#000}
 .screen img.bgimg{position:absolute;left:0;top:0;width:480px;height:640px;image-rendering:pixelated}
 .w{position:absolute;box-sizing:border-box;font-family:Consolas,monospace;color:#E0E4E8}
 .tag{position:absolute;font-size:10.5px;font-family:Consolas,monospace;color:#fff;background:rgba(0,0,0,.6);padding:0 3px;border-radius:2px;white-space:nowrap}
 .topbar{display:flex;align-items:center;justify-content:space-between;padding:0 8px;font-size:13px;color:#E0E4E8}
 .ring{position:absolute;border-radius:50%;background:conic-gradient(from 210deg,#00C3BC 0deg 240deg,rgba(255,255,255,.10) 240deg 360deg);
   mask:radial-gradient(farthest-side,transparent 0 58%,#000 59% 100%);-webkit-mask:radial-gradient(farthest-side,transparent 0 58%,#000 59% 100%)}
 .bignum{position:absolute;text-align:center;font-family:"Segoe UI",sans-serif;font-weight:600;color:#fff}
 .unit{position:absolute;text-align:center;font-size:12px;color:#9AA3AC}
 .card{position:absolute;border-radius:12px;background:rgba(10,14,18,.72);border:1px solid rgba(255,255,255,.10);padding:6px 8px}
 .card .k{font-size:10.5px;color:#8E97A0}
 .card .v{font-size:20px;color:#fff;font-weight:600;line-height:1.15}
 .card .v small{font-size:11px;color:#9AA3AC;font-weight:400}
 .bar{position:absolute;border-radius:6px;background:rgba(255,255,255,.12);overflow:hidden}
 .bar i{position:absolute;left:0;top:0;bottom:0;background:#C6FF3D}
 .led{position:absolute;border-radius:50%;background:#00C853}
 .dot{position:absolute;border-radius:50%;background:rgba(255,255,255,.35)}
 .dot.on{background:#00C3BC}
 table{border-collapse:collapse;font-size:12.5px;margin-top:6px;width:100%}
 th,td{border:1px solid #e3e3e3;padding:3px 8px;text-align:left;vertical-align:top}
 th{background:#f2f2f2;font-weight:600} td.num{text-align:right;font-family:Consolas,monospace;white-space:nowrap}
 code{background:#eee;padding:0 4px;border-radius:3px;font-size:12px}
 .note{font-size:12.5px;color:#555;line-height:1.85;margin-top:10px}
 .ok{background:#e6f4ea;color:#1e7a3c;border:1px solid #b7dfc4;padding:1px 6px;border-radius:3px;font-size:11px}
 .no{background:#fdecea;color:#a12a1c;border:1px solid #f2c0b8;padding:1px 6px;border-radius:3px;font-size:11px}
 .warn{background:#fff8e1;color:#8a6100;border:1px solid #f0dcab;padding:1px 6px;border-radius:3px;font-size:11px}
 .pick{background:#eef4fd;color:#1d4f91;border:1px solid #c6dbf7;padding:1px 6px;border-radius:3px;font-size:11px}
"""

def px(v): return v * S

def div(x, y, w, h, cls, inner=""):
    return (f'<div class="w {cls}" style="left:{px(x)}px;top:{px(y)}px;'
            f'width:{px(w)}px;height:{px(h)}px">{inner}</div>')

def tag(x, y, text):
    return f'<div class="tag" style="left:{px(x)+3}px;top:{px(y)+3}px">{text}</div>'

def dots(active):
    out = ""
    for i in range(2):
        cls = "dot on" if i == active else "dot"
        size = 5 if i == active else 4
        out += div(112 + i * 12, 309, size, size, cls)
    return out

def page1():
    o = []
    o.append(div(0, 0, 240, 24, "topbar", "<span>sm_blinky</span><span>cpu 26 %</span>"))
    o.append(div(210, 7, 10, 10, "led"))
    o.append(tag(0, 0, "顶栏 240x24 · title + cpu 读数 + lv_led(10x10) 闪烁（跟板载 LED 同步）"))
    o.append(div(40, 28, 160, 160, "ring"))
    o.append(tag(40, 28, "lv_arc 160x160 环宽12 扫角240°"))
    o.append(div(80, 84, 80, 48, "bignum", '<span style="font-size:44px">72</span>'))
    o.append(div(80, 132, 80, 16, "unit", "edges/s"))
    o.append(tag(80, 84, "大数字 + 单位"))
    o.append(div(12, 204, 104, 92, "card", '<div class="k">LED</div><div class="v">ON</div>'
                 '<div class="k" style="margin-top:6px">beat</div><div class="v">500<small> ms</small></div>'))
    o.append(tag(12, 204, "卡 A 104x92 · 状态"))
    o.append(div(124, 204, 104, 92, "card", '<div class="k">edges</div><div class="v">1234</div>'
                 '<div class="k" style="margin-top:6px">frame</div><div class="v">12.5<small> ms</small></div>'))
    o.append(tag(124, 204, "卡 B 104x92 · 计数/帧时"))
    o.append(div(16, 302, 96, 14, "bar", '<i style="width:62%"></i>'))
    o.append(div(16, 302, 96, 14, "", '<div style="font-size:10px;line-height:14px;padding-left:4px">assist 62%</div>'))
    o.append(div(176, 304, 48, 16, "unit", "12.5 ms"))
    o.append(tag(176, 304, "底栏"))
    o.append(dots(0))
    return "".join(o)

def page2():
    o = []
    o.append(div(0, 0, 240, 24, "topbar", "<span>STATS</span><span>cpu 26 %</span>"))
    o.append(div(210, 7, 10, 10, "led"))
    o.append(tag(0, 0, "顶栏同页 1（cpu 读数两页共用 ✓）"))
    o.append(div(40, 28, 160, 160, "ring"))
    o.append(tag(40, 28, "同位置：环形/趋势图 160x160（值变时一次性重绘）"))
    o.append(div(64, 76, 112, 22, "bignum", '<span style="font-size:26px">3,214</span>'))
    o.append(div(64, 102, 112, 16, "unit", "total edges"))
    o.append(div(12, 204, 104, 92, "card", '<div class="k">UI stack free</div><div class="v">1320<small> w</small></div>'
                 '<div class="k" style="margin-top:6px">LVGL pool</div><div class="v">18<small> KB</small></div>'))
    o.append(tag(12, 204, "卡 A · 真值（栈高水位 + 池占用）"))
    o.append(div(124, 204, 104, 92, "card", '<div class="k">frame min</div><div class="v">11.9<small> ms</small></div>'
                 '<div class="k" style="margin-top:6px">frame max</div><div class="v">46.3<small> ms</small></div>'))
    o.append(tag(124, 204, "卡 B · 帧时统计"))
    o.append(div(16, 302, 96, 14, "bar", '<i style="width:38%"></i>'))
    o.append(div(16, 302, 96, 14, "", '<div style="font-size:10px;line-height:14px;padding-left:4px">pool 18%</div>'))
    o.append(div(176, 304, 48, 16, "unit", "1 / 2"))
    o.append(tag(176, 304, "底栏"))
    o.append(dots(1))
    return "".join(o)

def screen(bg, widgets, title):
    return (f'<div class="frame"><div class="screen">'
            f'<img class="bgimg" src="bg/{bg}.png">'
            f'{widgets}</div><div class="cap" style="font-size:12px;color:#555;margin-top:6px">{title}</div></div>')

ROWS = [
    ("1", "顶栏文字", "(0,0)-(240,24)", "常量", "不动", "-", "-"),
    ("1+2", "lv_led（两页共用）", "(210,7) 10x10", "AO beat（ledOn）", "lv_led_on/off，跟板载 LED 同步闪烁（不再呼吸）", "100", "0.1 / 每 500 ms 一次"),
    ("1", "lv_arc", "(40,28) 160x160", "edges 映射 0..100", "值变时一次性重绘（160x160 ⇒ 34.8 ms，太贵不补间）", "25,600", "34.8 / 105%"),
    ("1", "大数字", "(80,84) 80x48", "同上", "文本替换（值变时）", "~3,800", "4.6 / 14%"),
    ("1", "单位", "(80,132) 80x16", "常量", "不动", "-", "-"),
    ("1", "卡 A 状态", "(12,204) 104x92", "ledOn", "bg_color + text，走 lv_style_transition 200 ms", "9,568", "11.5 / 35%"),
    ("1", "卡 B 计数/帧时", "(124,204) 104x92", "edges + 自测帧时", "文本替换", "~2,000", "2.4 / 7%"),
    ("1", "进度条", "(16,302) 96x14", "beat 占空比 / 池占用（待定）", "lv_bar_set_value(..., LV_ANIM_ON)", "1,344", "1.6 / 5%"),
    ("1", "页指示点 x2", "(112,309)", "当前页", "style transition（缩放/颜色）", "~200", "0.2 / 1%"),
    ("2", "lv_arc → 环形/趋势", "(40,28) 160x160", "总量/趋势数组", "值变时一次性重绘（同页 1）", "25,600", "34.8 / 105%"),
    ("2", "大数字 + 单位", "(64,76) 112x38", "总边沿数", "文本替换", "~4,000", "4.8 / 15%"),
    ("2", "卡 A 真值", "(12,204) 104x92", "uxTaskGetStackHighWaterMark / lv_mem_monitor（池占用）", "文本替换", "~2,000", "2.4 / 7%"),
    ("2", "卡 B 帧时统计", "(124,204) 104x92", "UI 线程自测 min/max", "文本替换", "~2,000", "2.4 / 7%"),
    ("2", "进度条 = 池占用", "(16,302) 96x14", "lv_mem_monitor", "LV_ANIM_ON", "1,344", "1.6 / 5%"),
]
rows = "".join(
    f'<tr><td class="num">{p}</td><td>{w}</td><td>{box}</td><td>{src}</td><td>{anim}</td>'
    f'<td class="num">{dirty}</td><td class="num">{cost}</td></tr>' for p, w, box, src, anim, dirty, cost in ROWS)

html = f"""<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>sm_blinky · 两页布局 v1（仪表 / 状态，来回切）</title>
<style>{CSS}</style></head><body>
<h1>sm_blinky · 两页布局 v1：仪表页 + 状态页（来回切）</h1>
<div class="sub">
<b>约束（按你的要求）</b>：只用 LVGL 现成部件 + 背景图；<b>不做资产动画</b>（无烘焙帧、无 Lottie）。
两页共用 <code>gen_bg.py</code> 那套锚点（顶栏 y0–24 / 大环 (40,28)–(200,188) / 卡片 A、B y204–296 / 底栏 y304–320），
所以两页可以直接换成不同背景图（<b>页 1 = 槽 0 bg-a</b>、<b>页 2 = 槽 1 bg-b</b> —— 槽 1 一直是空的，正好用上 ✓）。<br>
<b>切换</b>：Splash（槽 2 logo，1.5 s）→ 页 1 → 页 2 → 页 1 …；板上没有按键也没接触摸，触发器只能用<b>定时或事件计数</b>（建议每 5 s，或每 8 个 LED 边沿）。<br>
<span class="no">整屏动画不可能</span>（76,800 px ≈ 104 ms = 317% 周期，charter §6 已定）⇒ 切页 = <b>一次性整帧重画</b>（实测 34.8 / 46.3 ms），不做滑动/淡入。
</div>

<h2>① 页 1「仪表」（背景 = 槽 0 bg-a 夜色仪表）</h2>
<div class="row">{screen("bg-a", page1(), "页 1 · 仪表：大环 + 两个状态卡 + 进度条 + 页指示点")}</div>

<h2>② 页 2「状态」（背景 = 槽 1 bg-b 网格科技）</h2>
<div class="row">{screen("bg-b", page2(), "页 2 · 状态：同位置换内容（环形/趋势 + 两个统计卡）")}</div>

<h2>③ 部件表（脏区与耗时按 charter §6 的估算系数：渲染 1.2 µs/px + 刷屏 0.16 µs/px）</h2>
<table>
<tr><th>页</th><th>部件</th><th>像素框</th><th>值来源</th><th>动画（LVGL 能力内）</th><th>脏区 px</th><th>估算 ms / 33 ms</th></tr>
{rows}
</table>
<div class="note">
<span class="warn">动画要算总和</span>：页 1 若"环 + 卡 A + 进度条"同时动 = 36.4 ms/帧 ⇒ <b>111% ✗ 太紧</b>；同时 1~2 个才安全（环那一次性 34.8 ms 必须独占一帧）。
⇒ 建议编排：<b>环只在数值变化时重绘（不补间）</b>；<b>进度条 + lv_led 常驻小动画</b>（1.7 ms / 5% ✓）；<b>卡 A 颜色过渡</b>跟着 ledOn 事件走。
</div>

<h2>④ 切页时序（来回切就是这一条）</h2>
<div class="note">
<code>Splash(1.5 s, 槽 2)</code> → <b>页 1</b>（整帧 34.8 ms）→ <i>N s / N 个边沿</i> → <b>页 2</b>（整帧 ~46 ms）→ … → 回页 1。<br>
每页 entry 老规矩不变：<code>lv_obj_clean</code> + 重建控件树 ⇒ 两页控件永不共存；背光已在首帧落定后点亮，切页不再动背光 ✓。
</div>

<h2>⑤ 开关与依赖</h2>
<div class="note">
<span class="ok">新增 lv_conf.h 开关：0 个</span> —— <code>LV_USE_ARC 1</code> 已开；<code>lv_bar</code> / <code>lv_led</code> / <code>lv_label</code> / <code>lv_obj</code> / <code>lv_image</code> 都是默认或已开；绘制注入（若要渐变填充）也只要核心功能。<br>
<span class="warn">唯一可能花 flash 的项：大数字字体</span> —— 现在只有 14 px 那档。三选一：① 沿用 14 px；② <b>只烘数字的大字号点阵</b>（10 个字、48 px ≈ 3 KB 量级）；③ 自绘 7 段（0 flash，~40 行绘制代码）。<br>
<span class="pick">背景槽</span>：页 1 = 槽 0、页 2 = 槽 1 ⇒ 要把 <code>UI_BG_SLOT</code> 从编译期常量改成"每页 entry 传入的槽号"（<code>Ui_dispDemo(slot)</code>），一行接口的事。
</div>

<h2>⑥ 要你拍的几件</h2>
<div class="note">
① <b>环的尺寸</b>：160×160（贴背景锚点，但只能"值变时重绘"）还是 96×96（可连续补间，但与背景画的环不同径）？<br>
② <b>大数字</b>：14 px 沿用 / 烘数字专用大字号（≈3 KB flash）/ 自绘 7 段？<br>
③ <b>页 2 的数据</b>：全用真值（总边沿、栈余量、帧时 min/max、池占用）还是掺假数据（更像仪表但没意义）？<br>
④ <b>切换触发器与节奏</b>：每 5 s 自动切 / 每 8 个边沿切 / 两者可配？<br>
⑤ <b>页 1 进度条表示什么</b>：beat 占空比（真值）/ 池占用（真值）/ assist 式装饰值（假）？
</div>
</body></html>"""
open('./pages-two.html', 'w', encoding='utf-8').write(html)
print("写出 ./pages-two.html")
