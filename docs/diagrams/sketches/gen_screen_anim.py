# -*- coding: utf-8 -*-
"""动画型画面布局草图 v2。
只用镜像里已存在的控件：lv_arc / lv_bar / lv_led / lv_line / lv_label / lv_anim
（实测符号：arc 5 / bar 7 / led 2 / line 2 / label 13 / anim 24；slider、switch、spinner、chart = 0）
脏区估算依据 charter §6 的实测：渲染 ≈1.2 µs/px、刷屏 ≈0.16 µs/px，动画周期 LV_DEF_REFR_PERIOD=33 ms。
"""
S = 2
W, H = 240, 320
RENDER, FLUSH, PERIOD = 1.2, 0.16, 33.0     # µs/px, µs/px, ms

# (名, x, y, w, h, 圆角, 填充, 文字, 字号px)
WIDGETS = [
    ("title", 12, 6, 64, 16, 0, None, "sm_blinky", 7),
    ("bar_top", 0, 0, 240, 28, 0, "#101418", "", 7),
    ("led", 216, 9, 10, 10, 5, "#00C853", "", 7),
    ("arc", 72, 44, 96, 96, 48, "arc", "", 7),
    ("arc_lbl", 96, 84, 48, 16, 0, None, "62 %", 7),
    ("blkA", 60, 160, 48, 48, 10, "#00C853", "LED", 7),
    ("blkB", 132, 160, 48, 48, 10, "#303840", "TMR", 7),
    ("bar", 16, 224, 208, 12, 6, "#00C853", "", 7),
    ("lbl_l", 16, 248, 64, 16, 0, None, "edges 1234", 7),
    ("lbl_r", 176, 248, 48, 16, 0, None, "12.3 ms", 7),
]

DIRTY = [   # (名, w, h, 备注)
    ("arc 96×96", 96, 96, "值动画（lv_anim）"),
    ("bar 208×12", 208, 12, "值动画"),
    ("方块 48×48", 48, 48, "颜色/亮度动画"),
    ("lv_led 10×10", 10, 10, "呼吸灯"),
    ("label ~64×16", 64, 16, "文字变化"),
    ("整屏 240×320", 240, 320, "整屏动画（对照，不可行）"),
]

def draw(led_on, dirty=False):
    led_c = "#00C853" if led_on else "#303840"
    parts = []
    for name, x, y, w, h, r, fill, txt, fs in WIDGETS:
        f = fill
        if f == "arc":
            pct = 62
            inner = "#000000"
            parts.append(f'<div class="arc" style="left:{x*S}px;top:{y*S}px;width:{w*S}px;height:{h*S}px;'
                         f'background:conic-gradient({led_c} 0 {pct}%, #2a3038 {pct}% 100%)">'
                         f'<div class="arcin" style="background:{inner}"></div></div>')
            continue
        if name in ("blkA", "blkB"):
            f = led_c if name == "blkA" else "#303840"
        if name == "bar":
            parts.append(f'<div class="bar" style="left:{x*S}px;top:{y*S}px;width:{w*S}px;height:{h*S}px;'
                         f'border-radius:{r*S}px;background:#2a3038">'
                         f'<div style="width:62%;height:100%;border-radius:{r*S}px;background:{led_c}"></div></div>')
            continue
        if name == "led":
            f = led_c
        style = f'left:{x*S}px;top:{y*S}px;width:{w*S}px;height:{h*S}px;'
        if r: style += f'border-radius:{r*S}px;'
        if f: style += f'background:{f};'
        cls = "w"
        if name == "bar_top": cls += " topbar"
        if fill is None: cls += " lblbox"
        if name in ("title",): cls += " leftal"
        col = {"title":"#E0E4E8","arc_lbl":"#E0E4E8","lbl_l":"#808890","lbl_r":"#808890",
               "blkA":"rgba(0,0,0,.55)","blkB":"#C8CDD2"}.get(name, "#333")
        parts.append(f'<div class="{cls}" style="{style}color:{col};font-size:{fs*S}px">{txt}</div>')
    d = ""
    if dirty:
        for name, w, h, note in DIRTY[:-1]:
            m = dict((n, (x,y,ww,hh)) for n,x,y,ww,hh,_,_,_,_ in WIDGETS)[{"arc 96×96":"arc","bar 208×12":"bar","方块 48×48":"blkA","lv_led 10×10":"led","label ~64×16":"lbl_l"}[name]]
            x,y,ww,hh = m
            us = (w*h)*(RENDER+FLUSH)
            d += (f'<div class="dirty" style="left:{x*S}px;top:{y*S}px;width:{w*S}px;height:{h*S}px"></div>'
                  f'<div class="dirtag" style="left:{x*S+3}px;top:{y*S+3}px">{w}×{h} ≈ {us/1000:.1f} ms</div>')
    return parts, d

p1, _ = draw(True); p2, _ = draw(False); p3, d3 = draw(True, True)

def frame(parts, dirty, cap):
    return (f'<div class="frame"><div class="screen">'
            + "".join(parts) + dirty +
            f'</div><div class="cap">{cap}</div></div>')

rows = ""
for name, w, h, note in DIRTY:
    us = w*h*(RENDER+FLUSH); pct = us/1000/PERIOD*100
    verdict = ("✓✓ 随便动" if pct < 15 else "✓ 可以连续动画" if pct < 45
               else "△ 只做一次性过渡" if pct < 80 else "✗ 不可行")
    rows += (f'<tr><td>{name}</td><td class="num">{w*h:,}</td><td class="num">{us/1000:.1f} ms</td>'
             f'<td class="num">{pct:.0f}%</td><td>{verdict}</td><td>{note}</td></tr>')

html = f'''<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>sm_blinky · 动画型画面布局（v2）</title>
<style>
 body{{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#f5f5f5;color:#222;margin:0;padding:24px 32px 64px}}
 h1{{font-size:20px;margin:0 0 4px}} .sub{{color:#666;font-size:13px;margin-bottom:20px;line-height:1.7}}
 h2{{font-size:15px;margin:28px 0 10px;border-bottom:1px solid #ddd;padding-bottom:4px}}
 .row{{display:flex;gap:28px;flex-wrap:wrap;align-items:flex-start}}
 .frame{{text-align:center}}
 .screen{{position:relative;width:{W*S}px;height:{H*S}px;background:#000;border:1px solid #999;
          box-shadow:0 2px 10px rgba(0,0,0,.18);overflow:hidden}}
 .w{{position:absolute;display:flex;align-items:center;justify-content:center;font-family:Consolas,monospace}}
 .w.leftal{{justify-content:flex-start;padding-left:{6*S}px;box-sizing:border-box}}
 .lblbox{{border:1px dashed rgba(150,150,150,.6);border-radius:2px}}
 .topbar{{border-bottom:1px solid #222}}
 .arc{{position:absolute;border-radius:50%;display:flex;align-items:center;justify-content:center}}
 .arcin{{width:74%;height:74%;border-radius:50%}}
 .bar{{position:absolute;overflow:hidden}}
 .dirty{{position:absolute;border:2px dashed rgba(255,60,60,.9);background:rgba(255,60,60,.10)}}
 .dirtag{{position:absolute;font-size:11px;color:#ffdede;background:rgba(180,20,20,.85);
          padding:0 4px;border-radius:2px;font-family:Consolas,monospace}}
 .cap{{font-size:12px;color:#555;margin-top:6px}}
 table{{border-collapse:collapse;font-size:12.5px;margin-top:6px;width:100%}}
 th,td{{border:1px solid #e3e3e3;padding:3px 8px;text-align:left;vertical-align:top}}
 th{{background:#f2f2f2;font-weight:600}} td.num{{text-align:right;font-family:Consolas,monospace}}
 code{{background:#eee;padding:0 4px;border-radius:3px;font-size:12px}}
 .note{{font-size:12.5px;color:#555;line-height:1.7;margin-top:10px}}
 .ok{{background:#e6f4ea;color:#1e7a3c;border:1px solid #b7dfc4;padding:1px 6px;border-radius:3px;font-size:11px}}
 .no{{background:#fdecea;color:#a12a1c;border:1px solid #f2c0b8;padding:1px 6px;border-radius:3px;font-size:11px}}
</style></head><body>
<h1>sm_blinky · 动画型画面布局（v2）</h1>
<div class="sub">
<b>只用镜像里已存在的控件</b>：<code>lv_arc</code> / <code>lv_bar</code> / <code>lv_led</code> / <code>lv_line</code> / <code>lv_label</code> / <code>lv_anim</code>
（实测符号数：arc 5、bar 7、led 2、line 2、label 13、anim 24；<b>slider / switch / spinner / chart 都是 0</b> → 想用要先开 <code>LV_USE_*</code>，会加代码体积）。<br>
2× 放大，虚线框=文本尺寸估算；动画周期取 <code>LV_DEF_REFR_PERIOD = 33 ms</code>（30 fps，lv_conf_internal.h:255）。
</div>

<h2>① 动画型仪表页（LED ON）</h2>
<div class="row">{frame(p1, "", "标题条 + 弧 + 两个方块 + 进度条")}</div>

<h2>② LED OFF</h2>
<div class="row">{frame(p2, "", "同一套控件，颜色与数值变")}</div>

<h2>③ 脏区（每帧要重绘的像素）—— 这才是动画的预算</h2>
<div class="row">{frame(p3, d3, "红框=动画时要重绘的区域，标注是该块的估算耗时")}</div>
<table>
<tr><th>控件</th><th>脏区 px</th><th>估算/帧</th><th>33 ms 周期占用</th><th>结论</th><th>说明</th></tr>
{rows}
</table>
<div class="note">
估算公式：<code>脏区 px × (1.2 µs 渲染 + 0.16 µs 刷屏)</code>，两个系数来自 charter §6 的实测（方块 72×72 = 5,184 px → 渲染 ≈6 ms；每行字 72×24 = 1,728 px → ≈2.2 ms；flush 1.35 ms / 8,640 px）。<br>
<b>结论</b>：弧 ≤ 96×96（≈9,200 px）可以连续动画；132×132 的弧（17,424 px）只能做一次性过渡；<b>整屏动画（76,800 px ≈104 ms）在这颗片子上不可能</b> —— 上限约 9.6 fps。<br>
<b>多控件同时动画要算总和</b>：弧 + 进度条 = 11,712 px ≈ 16 ms ≈ 48% ✓ 可行；再叠两个方块就到 76% ✗ 太紧。
</div>

<h2>④ 控件清单（绝对定位，无 layout 引擎 —— <code>LV_USE_FLEX/GRID = 0</code>）</h2>
<table>
<tr><th>控件</th><th>像素框</th><th>用途</th><th>动画方式</th></tr>
<tr><td>label "sm_blinky"</td><td>(12,6)–(76,22)</td><td>标题</td><td>不动</td></tr>
<tr><td>lv_led 10×10</td><td>(216,9)–(226,19)</td><td>心跳指示</td><td><code>lv_led_set_brightness</code> + lv_anim 呼吸</td></tr>
<tr><td><b>lv_arc 96×96</b></td><td>(72,44)–(168,140)</td><td>主指标（占位：边数/目标）</td><td><code>lv_arc_set_value</code> 走 lv_anim 缓动</td></tr>
<tr><td>label "62 %"</td><td>(96,84)–(144,100)</td><td>弧中心数值</td><td>跟弧一起刷</td></tr>
<tr><td>obj 48×48 ×2</td><td>(60,160)–(108,208)<br>(132,160)–(180,208)</td><td>LED / TMR 状态块</td><td>bg_color 过渡</td></tr>
<tr><td>lv_bar 208×12</td><td>(16,224)–(224,236)</td><td>进度/占用率</td><td><code>lv_bar_set_value</code> + 缓动</td></tr>
<tr><td>label ×2</td><td>(16,248) / (176,248)</td><td>边数、帧时间</td><td>数值变化时刷</td></tr>
</table>
<div class="note">
<span class="ok">今天就能写</span> 上面这套控件全部已在镜像里，不用改 <code>lv_conf.h</code>。<br>
<span class="no">要开开关</span> slider / switch / spinner / chart / image —— 每个都要在 <code>lv_conf.h</code> 打开并重新编译，代码体积会涨（text 现在 168.8 KB / 512 KB）。
</div>
</body></html>'''
open('./screen-anim.html','w',encoding='utf-8').write(html)
print("写出 ./screen-anim.html")
