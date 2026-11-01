# UI HSM 直驱显示组件

[UML 图表](uml/ui/ui-components.html)展示组件依赖和 LVGL 对象树。

UI 线程负责收件、派发、事件回收和 LVGL heartbeat/flush。
Sm_Ui 持有被动显示组件，按状态和事件决定如何操作它们。
没有 Sys/Live 页面对象，也没有中间的页面转发接口。

## 所有权与依赖

```text
UI 线程 → Sm_Ui HSM → 被动显示组件 → LVGL
                  → UiMetrics（资源采样）
                  → NVM（读取 RAM 快照）
```

Sm_Ui 中平铺 Surface、Topbar、Gauge、BarPanel、Dots、Dial、Switch、
Nvm 标签和 Ram 标签实例。对象实例采用静态存储，LVGL 控件按状态
进入/退出创建和销毁；这些是两个不同的生命周期。

组件封装自身 LVGL 对象、样式、显示缓存及更新方法。组件不订阅事件、
不创建定时器、不决定页面跳转、不读取业务存储。HSM 调用组件 API，
不直接拼装 LVGL 样式。BarPanel 是资源条显示能力，不是页面协调层。

Surface 也放在 widgets 下，负责内容容器和背景图片描述符。
它不清理其他组件；每个组件的 destroy 只删除自己创建的 LVGL 对象，
然后清空自身句柄，不调用 lv_obj_clean 清空借来的父容器。

## 状态管理生命周期

| 状态 | 创建与操作 | 退出 |
|---|---|---|
| TOP 初始化 | 初始化组件实例、指标采集，创建 Surface | — |
| Splash | 设置背景、背光，arm 展示期限 | disarm 展示期限 |
| Active | 创建 Topbar，采样，arm 公共刷新 | disarm、销毁 Topbar |
| Sys | 创建 Gauge、BarPanel、Dots，显示资源快照 | disarm、逐个销毁 |
| Live | 创建 Dial、Switch、Nvm、Ram、Dots | disarm、逐个销毁 |

Topbar 跨 Sys/Live 切换保留；两个状态互斥使用同一个 Dots 实例。
组件只在所属状态有效期间操作。已投递但晚到的圆弧超时在 Sys
不被消费，因此不会访问已经销毁的 Dial。

Sys/Live 分别消费自己的页面超时，期限仍为 5 s/10 s。
HSM 每 30 ms 消费圆弧超时，更新自身进度和方向，再调用
W_Dial_setValue。Dial 只显示传入百分比，不决定往返动画策略。

Active 每 500 ms 采样并更新公共顶部，再投递 UI_REFRESH_SIG。
Sys 在此事件中直接更新 Gauge/BarPanel，HSM 按快照 revision 去重。
Live 直接更新 Switch；公共顶部读取 Nvm_blinky 设置计数标签。
LED 事件及业务快照的原有归属不变，页面不修改 NVM 数据。

## 适配与资源

DisplayOperations 仍由 UI 线程实现并绑定，提供背光和 FPS。
时间事件仍由 HSM 直接持有并 arm/disarm；普通 UI 线程没有 SST_Task。
所有组件调用均发生在 UI 线程，符合 LV_OS_NONE 的单线程约束。

资源采样仍保留九项水位：五个任务栈、tracer、UI 收件箱、小池、中池。
Figma 布局显示 Blinky 栈空闲/已用两行，小池水位显示在短弧仪表中；
其他条目的数据接口保留。CPU 保持实时窗口口径。

## 状态栏局部刷新（2026-11-01）

新 Figma 资产经 Bootloader 写入后，从板端 SRAM 读回三张图片，
与本地 Sys、Live、Splash RGB565 文件逐字节一致。
用户仍观察到状态栏花屏，其余页面正常。

宿主 LVGL 的局部更新与同状态整屏重绘逐字节一致，未复现异常。
板端仅将 LED、计数、FPS、CPU 更新后的失效区域扩大为全屏宽度的
横带（y=0..27），重新编译烧录后，用户确认状态栏花屏消失。

继续在 MCU 上捕获发送前像素，并在 UI 线程内逐点读回状态栏 GRAM。
同状态局部更新与横带重绘的发送像素始终一致，但修复前的结果如下：

| 传输模式 | 窄矩形错误像素数（两次采样） | 横带错误像素数 |
|---|---|---|
| 自动选择 DMA 宽度 | 2061 / 3406 | 0 / 0 |
| 强制半字 DMA | 6447 / 6573 | 6654 / 6720 |
| CPU 逐像素写入 | 0 / 0 | 0 / 0 |

根因是 DMA 宽度重配置发生在通道仍使能时：HAL 完成中断只将软件
状态设为 READY，硬件 EN 仍为 1；随后对 PSIZE/MSIZE 的写入被忽略。
HAL_DMA_Start_IT 内部虽关闭通道，但已经晚于驱动修改宽度的动作。
因此按新宽度计算的传输次数与硬件实际使用的旧宽度不一致。
窄矩形更容易触发奇数像素的半字回退；整宽横带持续使用字传输，
因而掩盖问题。此故障不能归咎于 LVGL 不支持局部刷新。

直接寄存器实验：EN=1 时写 0x6541，CCR 仍为 0x6A41；关闭通道后
写 0x6540，CCR 正确变为 0x6540。最终在修改宽度之前关闭 DMA，
并断言宽度位读回一致。保留原有半字/字自动选择与 DMA 通知等待。
修复后，上述三种模式的两次采样均为 0 错误像素，窄矩形及横带
都与发送前像素完全一致。状态栏横带规避及临时 GRAM/RAM 探针移除。
正常版本重新编译烧录后，固件及三张 SRAM 资产逐字节读回匹配，
状态采样确认 Sys/Live 流转；用户确认恢复 label 局部刷新后无花屏。

## 原生局部刷新

Dial 和 Gauge 只通过 lv_arc_set_value 更新，不扩大失效区域。
标签、进度条、开关、LED 和背景同样交给 LVGL 的属性更新处理脏区。
Splash/切页依靠背景变化及对象树创建/销毁产生失效区域；移除
requestFrame、Ui_framePending_ 及强制整屏重绘链路。
应用 UI 代码不再调用 lv_obj_invalidate 或 lv_obj_invalidate_area。

这不要求每次刷新都很小：LVGL 会合并脏区，背景切换、较大的属性
变化或失效区域过多时仍可能重绘整个对象甚至整屏，由 LVGL 决定。
宿主测试比较 Sys、Live 空/满圆弧的自然刷新与同状态整屏重绘结果，
检查是否漏刷；整屏失效只出现在这项测试中。

2026-11-01 正常版本编译烧录后，用户确认开机、切页、圆弧和状态栏
均正常，并观察到 CPU 占用约为旧横带重绘版本的四分之一；此比例为
用户对屏幕读数的观察，不是相同采样窗口的仪器对照。
J-Link 约 16 s 采样观察到 Live→Sys→Live，tick 连续增长、三张资产
有效；Live 稳态完成刷新率约 30～31 FPS。

## 验证

运行 `python3 tests/ui_components/run.py`，使用真实 LVGL/HSM/TimeEvt
和收件箱进行 ASan/UBSan 宿主机验证。覆盖启动事件顺序、缺图、积压、
顶部跨页面保留、反复切页、圆弧往返及晚到超时、LED 与 NVM 显示。
退出检查验证各组件句柄清空，并确认同父容器的其他对象不被删除。

宿主测试使用 64 KB LVGL 池以容纳 64 位指针，不能代表 MCU 内存预算。
固件仍使用原有 24 KB LVGL 池。2026-10-29 正常固件编译烧录后，
板端确认进入 Live、资产有效和 NVM 恢复；用户确认画面、自动切页
和圆弧正常。此结果不代替最坏路径的栈或 LVGL 内存峰值测量。

## Figma 样式校准（2026-11-01）

依据本地 sm_blinky.fig 的 default（24:98）及 liveShow（81:493）。
布局与配色参考设计；字体按当前决定统一为 LVGL 内置字体。
数值读取运行时快照，不填入设计示例值。

| 部件 | 落地形态 |
|---|---|
| 公共顶部 | 分开的 FPS/CPU 数值与单位、无光晕 LED、真实业务计数 |
| 字体 | 全局统一 LVGL 内置 Montserrat 14 |
| Sys Blinky | 真实计数、Blinky 栈最低空闲及对应峰值已用，单位 word |
| Sys Small | 实际块字节数、总块数、历史最低空闲百分比 |
| Sys BOOTS | Nvm_bootCount 快照，由 Sys 持有标签生命周期 |
| Live | 270° 圆弧、6 px 圆头、34×16 蓝色开关及 12 px 白色滑块 |
| 背景 | Sys 卡片补 1 px 内边框；原图 FILL 裁剪、圆角及遮罩静态合成 |

Sys 仪表是 210°..330° 的 120° 短弧。两种弧在 Figma 中都是
INSIDE 描边，LVGL 使用原始 98/60 px 外框，移除主题内边距。
未使用对象缩放、页面截图或手动扩展脏区。
Live 不再创建设计中不存在的隐藏 RAM/NVM/分页点；数据采样和
相应组件接口保留。Surface 明确关闭滚动条。

背景源图与重建说明见 ../bg/figma/README.md。

### 字体与几何复核

当前全局字体由 app/bsp/lvgl/lv_conf.h 的 LV_FONT_DEFAULT 指定，
统一使用 LVGL 内置 Montserrat 14。组件不再覆盖字体。
已移除额外的字体 C 文件、声明、字形数据、字距校准数据及脚本。

原来为 10/12 px 字体设置的文字框，按统一字体放宽高度；较长标题
按内容确定宽度，CPU/FPS 数字框保留三位数空间，避免换行与裁字。
背景、圆弧、进度条、开关及数据接口保持原有分工。

`tests/ui_components/figma_geometry.json` 保留设计原始测量数据。
当前自动对照只检查非文字控件的外框；不再要求文字字宽和基线
匹配 Figma 的多套字体。测试另外递归确认所有 label 均使用
LV_FONT_DEFAULT，并检查标题与卡片标题容纳完整文字。
局部刷新与同状态整屏重绘的一致性验证继续保留。

本地 .fig 的参考图仍可查看：
[Sys](ui/figma/sys-reference.png)、[Live](ui/figma/live-reference.png)。
这些图片用于设计参考，不作为页面背景资产。

统一字体后仅执行宿主验证；此前的固件烧录和板端采样结果不代表
此版本已经上板验证。
