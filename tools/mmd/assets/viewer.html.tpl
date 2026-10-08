<!doctype html>
<html lang="zh">
<head>
<meta charset="utf-8">
<title>{{TITLE}}</title>
<style>
  html, body { margin:0; height:100%; background:#fff;
               font:14px/1.5 system-ui, "Segoe UI", sans-serif; }
  #bar { position:fixed; top:0; left:0; right:0; height:42px; z-index:20;
         display:flex; align-items:center; gap:8px; padding:0 12px;
         background:#f5f5f5; border-bottom:1px solid #ddd; box-sizing:border-box; }
  #bar button { font:inherit; padding:4px 10px; cursor:pointer;
                background:#fff; border:1px solid #bbb; border-radius:4px; }
  #bar button:hover { background:#eee; }
  #bar button.on { background:#dce9ff; border-color:#7aa7e0; }
  #hint { margin-left:auto; color:#777; }

  /* 固定的参与者头栏：和主图同一份 SVG 克隆，横向同步 */
  #head { position:fixed; top:42px; left:0; right:0; height:0; overflow:hidden;
          z-index:10; background:#fff; box-shadow:0 2px 6px rgba(0,0,0,.12);
          pointer-events:none; display:none; }
  #headstage { transform-origin:0 0; will-change:transform; padding:8px; }
  #headstage svg { max-width:none !important; height:auto !important; }

  #wrap { position:absolute; top:42px; left:0; right:0; bottom:0;
          overflow:hidden; cursor:grab; }
  #wrap.drag { cursor:grabbing; }
  #stage { transform-origin:0 0; will-change:transform; padding:8px; }
  /* mermaid 默认给 svg 加了 max-width，会阻止放大 —— 必须压掉 */
  #stage svg { max-width:none !important; height:auto !important; }

  #srcbox { position:absolute; top:42px; left:0; right:0; bottom:0;
            overflow:auto; margin:0; padding:16px; box-sizing:border-box;
            background:#fbfbfb;
            font:13px/1.6 ui-monospace, Consolas, monospace;
            white-space:pre; display:none; }
</style>
</head>
<body>
<div id="bar">
  <button data-act="all">全览</button>
  <button data-act="fitw">适应宽度</button>
  <button data-act="100">100%</button>
  <button data-act="in">＋</button>
  <button data-act="out">－</button>
  <button data-act="head" id="headbtn">固定头栏</button>
  <button data-act="src" id="srcbtn">源码</button>
  <span id="hint">滚轮缩放 · 按住拖动 · 双击复位</span>
</div>
<div id="head"><div id="headstage"></div></div>
<div id="wrap"><div id="stage"><pre class="mermaid">
{{SOURCE}}
</pre></div></div>
<pre id="srcbox"></pre>

<script src="mermaid.min.js"></script>
<script>
// 先留住源码 —— mermaid 渲染后会把 <pre class="mermaid"> 的内容换掉
var SRC = document.querySelector('.mermaid').textContent.trim();
document.getElementById('srcbox').textContent = SRC;

mermaid.initialize({
  startOnLoad: false,
  theme: 'default',
  sequence: {
    mirrorActors: false,   // 底部不再重复参与者条
    messageMargin: 22,
    noteMargin: 5,
    boxMargin: 5,
    actorMargin: 36,
    diagramMarginX: 6,
    diagramMarginY: 6
  }
});

mermaid.run({ querySelector: '.mermaid' }).then(setupView);

function setupView() {
  var wrap    = document.getElementById('wrap');
  var stage   = document.getElementById('stage');
  var head    = document.getElementById('head');
  var headstg = document.getElementById('headstage');
  var srcbox  = document.getElementById('srcbox');
  var srcbtn  = document.getElementById('srcbtn');
  var headbtn = document.getElementById('headbtn');
  var svg     = stage.querySelector('svg');
  var info    = document.getElementById('hint');

  var s = 1, tx = 0, ty = 0;
  var srcMode = false, headOn = false;
  var PAD = 8, HEAD_MAX = 120;

  // ── 先把 SVG 钉在它的自然尺寸上 ────────────────────────────────
  // mermaid 给的是 width="100%"：只压 max-width 不够，svg 仍会被拉伸到容器宽，
  // 于是所有按 s 算的几何（头栏对齐、钳位）全错。
  var vb = svg.viewBox.baseVal;
  svg.removeAttribute('width');
  svg.removeAttribute('height');
  svg.style.width  = vb.width  + 'px';
  svg.style.height = vb.height + 'px';

  // ── 固定头栏：克隆主 SVG，裁出顶部参与者那一带 ──────────────────
  // 非序列图（没有 .actor-top）没有"参与者行"可钉，直接禁用头栏
  var clone = svg.cloneNode(true);
  clone.removeAttribute('id');
  headstg.appendChild(clone);

  var actors = svg.querySelectorAll('.actor-top');
  var hasActors = actors.length > 0;
  var bandTop = Infinity, bandBot = -Infinity;
  actors.forEach(function (el) {
    var b = el.getBBox();
    if (b.y < bandTop) bandTop = b.y;
    if (b.y + b.height > bandBot) bandBot = b.y + b.height;
  });
  if (!isFinite(bandTop)) { bandTop = 0; bandBot = 40; }

  // getBBox 给的是用户坐标，viewBox 起点未必是 0（mermaid 是 -6,-6），
  // 转成 svg 内的像素位置要把原点减掉
  function bandTopPx() { return (bandTop - vb.y) * s; }
  function bandBotPx() { return (bandBot - vb.y) * s; }

  function applyHead() {
    if (!headOn) { head.style.display = 'none'; return; }
    head.style.display = 'block';
    var h = Math.min(HEAD_MAX, bandBotPx() - bandTopPx() + PAD * 2);
    head.style.height = h + 'px';
    // 横向跟随主图；纵向把参与者那一带钉在头栏顶部
    headstg.style.transform =
      'translate(' + tx + 'px,' + (PAD - bandTopPx()) + 'px) scale(' + s + ')';
  }

  // 头栏开着时，图自己的那条参与者行必须永远躲在头栏后面 ——
  // 所以 ty 有个上限：图的参与者带顶端不能低于头栏里那条的位置
  function tyMax() { return headOn ? (PAD - bandTopPx()) : Infinity; }

  function apply() {
    if (ty > tyMax()) { ty = tyMax(); }
    stage.style.transform = 'translate(' + tx + 'px,' + ty + 'px) scale(' + s + ')';
    applyHead();
    if (!srcMode) {
      info.textContent = Math.round(s * 100) + '%  ·  滚轮缩放 · 按住拖动 · 双击复位';
    }
  }
  function size() {
    var r = svg.getBoundingClientRect();
    return { w: r.width / s, h: r.height / s };
  }
  function fitAll() {
    var d = size(), k = Math.min(wrap.clientWidth / d.w, wrap.clientHeight / d.h) * 0.97;
    s = k; tx = (wrap.clientWidth - d.w * k) / 2; ty = (wrap.clientHeight - d.h * k) / 2; apply();
  }
  function fitWidth() {
    var d = size(), k = wrap.clientWidth / d.w * 0.97;
    s = k; tx = (wrap.clientWidth - d.w * k) / 2; ty = tyMax(); apply();
  }
  function reset() { s = 1; tx = 0; ty = 0; apply(); }

  function toggleSrc() {
    srcMode = !srcMode;
    srcbox.style.display = srcMode ? 'block' : 'none';
    wrap.style.display    = srcMode ? 'none'  : 'block';
    srcbtn.classList.toggle('on', srcMode);
    if (srcMode) { head.style.display = 'none'; }
    info.textContent = srcMode ? '源码模式 —— 可框选复制' : '';
    if (!srcMode) apply();
  }
  function toggleHead() {
    headOn = !headOn;
    headbtn.classList.toggle('on', headOn);
    applyHead();
  }

  document.getElementById('bar').addEventListener('click', function (e) {
    var act = e.target.getAttribute('data-act');
    if (!act) return;
    if (act === 'src')  { toggleSrc();  return; }
    if (act === 'head') { toggleHead(); return; }
    if (srcMode) return;
    if (act === 'all')  fitAll();
    if (act === 'fitw') fitWidth();
    if (act === '100')  reset();
    if (act === 'in')  { s *= 1.25; apply(); }
    if (act === 'out') { s /= 1.25; apply(); }
  });

  // 滚轮缩放（以光标为中心）
  wrap.addEventListener('wheel', function (e) {
    e.preventDefault();
    var k = e.deltaY < 0 ? 1.12 : 1 / 1.12;
    var r = wrap.getBoundingClientRect();
    var px = e.clientX - r.left, py = e.clientY - r.top;
    tx = px - (px - tx) * k;
    ty = py - (py - ty) * k;
    s *= k;
    apply();
  }, { passive: false });

  // 拖动平移
  var dragging = false, ox = 0, oy = 0;
  wrap.addEventListener('pointerdown', function (e) {
    dragging = true; ox = e.clientX - tx; oy = e.clientY - ty;
    wrap.classList.add('drag'); wrap.setPointerCapture(e.pointerId);
  });
  wrap.addEventListener('pointermove', function (e) {
    if (!dragging) return;
    tx = e.clientX - ox; ty = e.clientY - oy; apply();
  });
  wrap.addEventListener('pointerup', function (e) {
    dragging = false; wrap.classList.remove('drag');
    wrap.releasePointerCapture(e.pointerId);
  });
  wrap.addEventListener('dblclick', reset);

  // 键盘：0=全览 1=100% +/-=缩放 h=头栏 s=源码
  window.addEventListener('keydown', function (e) {
    if (e.key === 's' || e.key === 'S') { toggleSrc();  return; }
    if (e.key === 'h' || e.key === 'H') { toggleHead(); return; }
    if (srcMode) return;
    if (e.key === '0') fitAll();
    if (e.key === '1') reset();
    if (e.key === '+' || e.key === '=') { s *= 1.25; apply(); }
    if (e.key === '-') { s /= 1.25; apply(); }
  });

  // 默认打开固定头栏；非序列图没有参与者行可钉，按钮直接隐藏
  if (hasActors) {
    headOn = true;
    headbtn.classList.add('on');
  } else {
    headOn = false;
    headbtn.style.display = 'none';
  }
  fitWidth();
}
</script>
</body>
</html>
