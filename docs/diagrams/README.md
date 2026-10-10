# 时序图与设计草图

这一目录是**图的源与产物**：`.mmd` 是 mermaid 文本（源），同名 `.html` 是自包含查看器（产物，用仓库自带的
`tools/mmd/` 生成）。所有图都在**仓库里**，不再依赖 `/mnt/c/tmp_atk/`（那只是 Windows 侧的查看副本）。

## 图清单

| 文件 | 视角 | 内容 |
|---|---|---|
| `init.html` / `.mmd` | **现有固件**启动（对象级） | 12 参与者 / 31 消息，带 `file:line` —— 改代码后要重新核对的**底稿** |
| `init-pkg.html` / `.mmd` | 现有固件启动（包级） | 9 个顶层包 + AO 顺序，颗粒度对齐 StarUML 里那张 `BSP_init` |
| `bsp-init.html` / `.mmd` | BSP 初始化 | 面板 / SRAM / NOR 三条支路（早先那版） |
| `bsp-init-brief.html` / `.mmd` | BSP 初始化（简） | 一屏版 |
| `bsp-elements.html` / `.mmd` | BSP 组成 | 元素关系，非时序图 |
| `panel-init-loop.html` / `.mmd` | 写法样例 | 面板初始化的 `loop` / `opt` / `alt` 怎么画 |
| `uart-recv-fsm.html` / `.mmd` | **待落地**：串口烧写接收状态机 | 11 状态（S1 等握手 … S9 DONE / SX ABORT），非时序图 |
| `bootloader-flow.html` / `.mmd` | **待落地**：BL 每次上电做什么 | 标志/窗口/正常启动三分支 + 跳转契约（VTOR/MSP） |
| `asset-burn-bootloader.html` / `.mmd` | **待落地**：烧写全程 | 单脚本管全程：开串口 → J-Link 置标志复位 → 握手 → 38 块 → DONE |
| `serial-script-flow.html` / `.mmd` | **待落地**：脚本侧流程 | 脚本自己的步骤、外部输入、重试与诊断 |
| `asset-load.html` / `.mmd` | **待落地**：app 运行期 | 启动期把背景资产一次性搬进外挂 SRAM |
| `ui-flush.html` / `.mmd` | **现有固件**刷屏（对象级） | UI 线程一次刷屏的完整策略：渲染 → 开窗 → 起 DMA → 线程阻塞（idle 泵日志）→ 中断唤醒 → flush_ready，含超时/错误断言与"单缓冲不重叠"的说明 |
| `flash-log-write.html` / `.mmd` | **待落地（草案）**：运行期写 flash | 追加日志 + 小桶：生产者 → RAM 暂存环（不阻塞）→ flash 线程 → 256 B 页缓冲 → 预擦/编程/回读；擦除 130~160 ms 与 BUSY 等标了"阻塞等"的位置，DMA 写只标成将来那一步 |
| `flash-log-read.html` / `.mmd` | **待落地（草案）**：运行期读 flash | 桶读：RAM 索引 → 现有 RPC → `BSP_flashReadDma` → DMA1_Ch4 → 槽 1 唤醒；读路径已经是 DMA（§12），写侧接 DMA 不影响它 |

`sketches/` 里是设计草图（非时序图）：`layout.html`（存储布局：NOR 分区 / 外挂 SRAM / 资产区）、
`screen-layout.html`（现有 demo 画面）、`screen-anim.html`（动画型 v2）、`screen-ebike.html`（照 LVGL e-bike 改造的 v3）、
`bg-preview.html` + `bg/*.png`（三张整屏背景候选）。

## 生成 / 校验 / 打开

```sh
# 生成（会在输出目录备好 mermaid.min.js；离线可用，缓存 ~/.cache/mermaid-master/）
tools/mmd/mmd2html.py docs/diagrams/init-pkg.mmd            # 默认同名 .html
tools/mmd/mmd2html.py docs/diagrams/init-pkg.mmd -o /mnt/c/tmp_atk/init-pkg.html -t "标题"

# 校验（静态扫描 + 无头 Chrome 真渲染核对条数；退出码 0 = 全过）
tools/mmd/mmd_check.py docs/diagrams/*.mmd

# 打开（Windows 侧）
/mnt/c/Windows/System32/cmd.exe /c start "" "C:\tmp_atk\init-pkg.html"
```

## 转录到 StarUML / PlantUML

取 `.mmd` 原文，**删掉第一行 `%%{init: ...}%%`**（那是 mermaid 专用指令，别的工具会当语法错误）。
`tools/mmd/mmd2html.py` 会自动注入这一行；`.mmd` 源里通常没有。

## 三条踩过的坑（工具已挡住两条）

1. **正文里的 `<`**：查看器把源码放进 HTML，不转义时 `<X>` 被当标签吞掉 → 整张图 Syntax error。
   → `mmd2html.py` 现在**插入前转义源码**（`html.escape`），所以 `<` 与 `<br/>` 都安全 ✓
2. **`participant Link`**：撞 mermaid 保留字（`link`/`linkStyle`）→ 含它的每条消息都失败。换个名字（如 `Serial`）✓
   → `mmd_check.py` 会**报错**挡下这一条（这是 mermaid 本身的限制，不是嵌入问题）
3. **多行 `note ... end note`**：在**未转义**的嵌入下必失败；转义后可用 ✓ → 修 ① 之后这条也一起解了

另外：**原始渲染是 1 条消息 = 1 个 `messageText`**；查看器模板为固定头栏把图渲染两遍，所以在浏览器里数是 2 倍 ——
`mmd_check.py` 按 1:1 核对，别把两种数字混起来。

## 不进 git 的东西

`mermaid.min.js`（3.5 MB）：`mmd2html.py` 会在输出目录按需放一份（优先用 `~/.cache/mermaid-master/`，否则从 CDN 取），
已在 `.gitignore` 里排除。所以**首次生成本目录的 HTML 时**它会出现在这里 —— 那是正常的，别提交。
