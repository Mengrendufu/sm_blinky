#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mermaid 时序图校验：先用纯 Python 扫"这个版本一定渲染不出来"的构造，再用无头 Chrome 真渲染一遍核对条数。

为什么需要它（三条都是踩过的坑，见 AGENTS.md 与 docs/diagrams/README.md）：
  1. 正文里出现 `<` → 查看器把源码塞进 HTML，`<XXX>` 会被当标签吞掉 → 整张图语法错误；
  2. participant 名叫 `Link` → 撞 mermaid 保留字（link/linkStyle）→ 含它的每条消息都失败；
  3. 多行 `note ... end note` → 这一版（11.17.2）必失败；只有单行 `Note ...: 文本` 能用。

用法:
    tools/mmd/mmd_check.py docs/diagrams/*.mmd
    tools/mmd/mmd_check.py --chrome "/mnt/c/Program Files/Google/Chrome/Application/chrome.exe" 图.mmd
退出码 0 = 全部通过。没找到 Chrome 时只跑静态检查（会明确说跳过渲染）。
"""
import argparse
import html as _html
import os
import re
import shutil
import subprocess
import sys
import tempfile

KEYWORDS = {"link", "links", "end", "note", "loop", "alt", "else", "opt", "par", "and",
            "critical", "option", "break", "rect", "activate", "deactivate", "autonumber",
            "box", "participant", "actor", "over", "title"}
CHROME_CANDIDATES = [
    "/mnt/c/Program Files/Google/Chrome/Application/chrome.exe",
    "/mnt/c/Program Files (x86)/Google/Chrome/Application/chrome.exe",
    "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
]
MERMAID_CACHE = os.path.expanduser("~/.cache/mermaid-master/mermaid.min.js")


def static_check(path, text):
    """返回 (错误列表, 警告列表)。"""
    errs, warns = [], []
    lines = text.splitlines()

    for i, line in enumerate(lines, 1):
        if "<" in line:
            warns.append(f"{path}:{i}: 正文里有 '<' —— 本仓库生成器已转义，安全；别的未转义渲染器会吞掉它")
        if ";" in line:
            warns.append(f"{path}:{i}: 有 ';'，在 mermaid 里可能是语句分隔符")
        s = line.strip()
        if re.match(r"^note\s+(right|left|over)\b", s, re.I) and not s[idx_of_colon(s):].strip(": "):
            warns.append(f"{path}:{i}: 多行 note —— 本仓库生成器已转义，可用；未转义渲染器会失败")

    if re.search(r"^\s*end note\s*$", text, re.M):
        warns.append(f"{path}: 出现 'end note' —— 多行 note，转义后可用（见注释）")

    for m in re.finditer(r"^\s*(?:participant|actor)\s+(\w+)\s*(?:as\b.*)?$", text, re.M):
        if m.group(1).lower() in KEYWORDS:
            errs.append(f"{path}: participant 名 '{m.group(1)}' 撞 mermaid 保留字 → 换名（如 Serial/Link2）")
    return errs, warns


def idx_of_colon(s):
    i = s.find(":")
    return i if i >= 0 else len(s)


def counts(text):
    msgs = [l for l in text.splitlines() if "->>" in l and "participant" not in l]
    notes = [l for l in text.splitlines() if l.strip().startswith("Note ")]
    brs = sum(l.count("<br/>") for l in notes)
    return len(msgs), len(notes), brs


def render_check(chrome, path, text):
    """把图塞进最小页面，用无头 Chrome 渲染，核对条数 + 是否出现 'Syntax error'。"""
    if not os.path.isfile(MERMAID_CACHE):
        return None, "没有 ~/.cache/mermaid-master/mermaid.min.js（先跑 tools/mmd/mmd2html.py 取一份）"
    page = ("<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body>"
            '<pre class="mermaid" id="t">' + _html.escape(text) + "</pre>"
            '<script src="mermaid.min.js"></script>'
            "<script>mermaid.initialize({startOnLoad:true});</script></body></html>")
    with tempfile.TemporaryDirectory() as d:
        shutil.copy(MERMAID_CACHE, os.path.join(d, "mermaid.min.js"))
        open(os.path.join(d, "t.html"), "w", encoding="utf-8").write(page)
        win = subprocess.run(["wslpath", "-w", os.path.join(d, "t.html")],
                             capture_output=True, text=True).stdout.strip()
        url = "file:///" + win.replace("\\", "/") if win else "file://" + os.path.join(d, "t.html")
        out = subprocess.run([chrome, "--headless=new", "--disable-gpu", "--no-sandbox",
                              "--virtual-time-budget=15000", "--dump-dom", url],
                             capture_output=True, text=True).stdout
    if "Syntax error" in out:
        return False, "Chrome 渲染报 Syntax error（静态检查没抓到，请肉眼核）"
    # 注意：原始渲染是 1:1（每条消息 1 个 messageText；每个 note 1 个，
    # 含 <br/> 时按行拆成多个）。查看器（viewer.html.tpl）会为固定头栏把图渲染两遍，
    # 所以它在浏览器里看到的是 2 倍 —— 别把两种数字混起来。
    got_msg = out.count('class="messageText')
    got_note = out.count('class="noteText')
    want_msg, want_notes, brs = counts(text)
    want_note = want_notes + brs
    if got_msg != want_msg:
        return False, f"消息条数不符：渲染 {got_msg} 条 vs 源码 {want_msg} 条"
    if got_note != want_note:
        return False, f"注释行数不符：渲染 {got_note} 行 vs 源码 {want_note} 行（{want_notes} 条 + {brs} 个换行）"
    return True, f"渲染 OK（消息 {want_msg} 条、注释 {want_notes} 条 / {want_note} 行）"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--chrome", default=None, help="Chrome/Chromium 可执行文件路径")
    ap.add_argument("--no-render", action="store_true", help="只做静态检查")
    a = ap.parse_args()

    chrome = a.chrome or next((c for c in CHROME_CANDIDATES if os.path.isfile(c)), None)
    if not a.no_render and not chrome:
        print("[mmd] 没找到 Chrome，跳过渲染检查（静态检查照跑）")

    bad = 0
    for path in a.files:
        text = open(path, encoding="utf-8").read()
        errs, warns = static_check(path, text)
        ok_render, note = (None, "跳过渲染")
        if not a.no_render and chrome:
            ok_render, note = render_check(chrome, path, text)
        status = "OK" if not errs and ok_render is not False else "FAIL"
        print(f"[{status}] {path}: {note}")
        for e in errs:
            print("       ✗ " + e)
        for w in warns:
            print("       ! " + w)
        if status == "FAIL":
            bad += 1
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
