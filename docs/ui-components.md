# UI 重构准备状态

## 组件尺寸参数

Topbar、PageTitle、BootCount、ThreadMonitor、ThreadCard、
ResourceMonitor、ResourceCard 的 create 接口统一在 parent 后接收
int32_t width / height；PageTitle 的 title 参数随后传入。
尺寸直接使用 LVGL 原生值：像素数或 lv_pct(100)，不另造 Fill 枚举。
例如 UiTopbar_create(&bar, root.container, lv_pct(100), 17)。
页面指定区域高度，宽度使用 Fill；Monitor 内部 viewport 和两张
卡片随父尺寸变化，第二张卡的位置也使用百分比，不缓存像素宽度。
ThreadMonitor 的 viewport 通过 flex_grow 获取剩余高度。
字体、LED、圆点和圆弧保持设计尺寸，不随外框缩放。
UiRoot 始终取屏幕尺寸；UiDots 的自然尺寸仍由点数决定。

当前只保留唯一持久可见的 UiRoot 内容根容器，Splash 复用其背景。
原 Sys/Live 的业务控件、背景挂载、资源采样、Header/Dial 时间事件
及相关数据展示调用已移除。资产格式、Flash 加载和 NVM 模块未改动。
原背景源图保存在 bg/figma；slot0/1 已由 SysInfo/LiveShow 挂载。

UI 线程继续负责收件、派发、事件回收和 LVGL heartbeat/flush。
Sm_Ui 保留启动就绪事件流转，以及 SysInfo 12 s、LiveShow 10 s 的
切页骨架。splashing 入口把 Splash 像素挂成根容器背景、出口还原
为黑。Active 入口在根容器下挂被动 Topbar，整个 demo 常驻，
SysInfo / LiveShow 共用同一个实例。两页入口各自把
slot0 / slot1 挂成根容器整屏背景，出口清掉，缺槽回退黑色。

当前链路：

```text
UI 线程 → Sm_Ui HSM → UiSplash → UiRoot → LVGL
                  → UiTopbar（Active entry 创建一次，常驻）
                  → UiSysInfo → UiRoot（背景+页标题+ThreadMonitor）
                  → splashTimeout（SPLASH_TIMEOUT_SIG，仅启动停留）
                  → pageSwitchTimeout（UI_PAGE_SWITCH_TIMEOUT_SIG，切页）
```

目录分两类助手：hsm/helpers 放 HSM 持有的无状态页面操作
（ui_splash.c/.h 让根容器借用验证过的 RGB565 像素；
ui_sys_info.c/.h 做 SysInfo 页的背景借用和页标题
build/hide（PageTitle 挂在根容器下，UiRoot_init 配置的
COLUMN flex 让它落在 Topbar 下方、行距 10）；
不动 Topbar 或兄弟节点；LiveShow 的
页面助手留待其实现落地时再建）。共享 Topbar 由
Active entry 直接创建一次、整个 demo 常驻——没有
生产环境的 Active exit，不做 exit 清理；组件
UiTopbar_destroy 仅保留给测试 teardown。
像素描述符由 UiRoot 持有；
hsm/widgets/helpers 放组件创建/配置助手（ui_flex、ui_label）。
widgets 下保留 ui_root/ui_root.c/.h、topbar/topbar.c/.h，
widgets/styles/ui_style.c/.h 放共享 const 样式。HSM 仍持有全部
状态与对象所有权；SysInfo 页面助手已存在（背景操作），
LiveShow 的实现落地时再建，不留空占位。

hsm/widgets/helpers/ui_flex.c/.h 是共享的 flex 布局创建助手：
FlexContainer_create(parent, &UiFlexConfig) 建一个去样式、透明、被动的
普通子对象，按配置设尺寸、flow、三向对齐与六个 pad
（padTop/Bottom/Left/Right 是内边距，padRow/Column 是轨道/
项目间距），尺寸原样转发（含 LV_SIZE_CONTENT/percent）。
config 只在调用期借用、不保留，自动变量或复合字面量均可；
返回对象归 LVGL/父级所有，用 lv_obj_delete 或随父级销毁。
Topbar 的三个 flex 容器（外框与左右分组）均改用它。

hsm/widgets/helpers/ui_bar.c/.h 薄封装原生 lv_bar：
Bar_create(parent, &BarConfig) 只创建一个对象，无子对象或标签；
配置 width/height、minValue/maxValue 和初值 value。
要求 minValue < maxValue，初值在范围内；尺寸原样转发，
支持 percent。配置只在调用期借用，初值使用 LV_ANIM_OFF，
保留原生 NORMAL 模式与 AUTO 方向；位置由父布局或调用方决定。
helper 清除默认样式且不挂业务外观，调用方分别挂载：

```c
lv_obj_add_style(bar, &UiStyle_barTrack, LV_PART_MAIN);
lv_obj_add_style(bar, &UiStyle_barIndicator, LV_PART_INDICATOR);
```

两份 const 样式仅管外观：圆头、白色，轨道 30% 不透明、
填充完全不透明；这是当前实现默认选择，不声称匹配 Figma 取色。
之后直接调用 lv_bar_set_value/range，遵循 LVGL 的数值运算边界。
仅在 UI 线程操作；对象归父级所有，可直接销毁或随父级销毁。
线程卡片已复用它；helper 不包含数值/单位文字、采样或自动动画。

hsm/widgets/helpers/ui_arc.c/.h 同层薄封装原生 lv_arc：
Arc_create(parent, &ArcConfig) 只创建轨道和指示条所在的单个对象，
无 knob、无文字或额外容器。配置尺寸、startAngle/endAngle、
minValue/maxValue 和初值 value；角度为顺时针 0..360 度，
起点大于终点允许跨零。范围须递增，初值须在范围内。
配置仅调用期借用；默认 NORMAL 模式、零旋转，关闭点击和滚动，
不挂外观，不负责位置。调用方按 MAIN / INDICATOR 挂载共享
UiStyle_arcTrack / UiStyle_arcIndicator：线宽 6、圆头、
白色轨道和 #BEE086 指示条。后续直接用 lv_arc_set_value/range/
bg_angles，不再另包更新接口；仅 UI 线程操作，父级拥有生命周期。
资源卡通过 Arc_create 组合圆弧，百分比标签留在卡片圆弧区。

hsm/widgets/helpers/ui_label.c/.h 是同形态的标签创建助手：
Label_create(parent, &LabelConfig) 建一个去样式的真
lv_label，只管布局与内容：尺寸、longMode，最后
lv_label_set_text_static 借用调用方缓冲（不复制；改完
缓冲须再调一次通知 LVGL）。config 只在调用期借用；
text 必须比 label 活得久。textAlign 是标签框内的水平
对齐：AUTO（0 默认）/LEFT/CENTER/RIGHT，只管文本，
不影响控件垂直方向或控件位置/对齐。
LV_LABEL_LONG_MODE_DOTS
被 SM_REQUIRE 拒绝——本版 LVGL 对静态文本不渲染 dots。
另有 ignoreLayout 指针字段：NULL（默认）完全不写控件
位置或控件对齐，Flex/Grid 父级照常决定；非 NULL 时挂
LV_OBJ_FLAG_IGNORE_LAYOUT 并 lv_obj_align TOP_LEFT 到
LabelIgnoreLayout 的 x/y（相对父级内容区左上，
边框与内边距以内；有符号/百分比原样转发）。
外观不归助手管：hsm/widgets/styles/ui_style.c 里的 const
lv_style_t（UiStyle_topbarText = Montserrat 14 白色文字属性组）
由组件创建后 lv_obj_add_style 挂到对象上，
程序期常驻、无需清理；Topbar 的三个标签
（blinkyCount/fps/cpu）都挂它。LED 圆角/颜色仍在
Topbar 内部，不进共享样式。

Topbar 是被动组件，由 Active entry 挂载并读取 NVM 边沿累计值；
LED 事件更新灯状态并重新读取累计值。
独立 1 s 时间事件更新 FPS 和 CPU，跨 SysInfo/LiveShow 持续运行；
FPS 取显示适配层实际完成帧率，静态画面不保证持续满帧。
CPU 对 BSP 的 idle/total 计数作窗口差分，显示非 idle 占比；
首个窗口前为 0，无新增采样时保留上次值。
UI 线程适配器在临界区读取计数对，Topbar 不承担采样逻辑。
LED 事件由 Active 状态处理；Splash 期间不更新 Topbar。
SysInfo 每次进入时读取 NvmBoot 的 RAM 启动次数：
7 个 LVGL 对象
（236x17 flex 行容器 + 左右两个透明分组 + LED + 三个静态文本
标签）；外框无 padding。UiRoot_init 设置四边 2 px 的页面内边距，
并配置纵向 Flex、START 对齐和 10 px 行距，Topbar 自然落在 (2,2)。
Active 入口仅创建 Topbar，不再配置 Root 的布局。
Splash 借用 Root 的整屏背景，不创建参与 padding 布局的子对象。
接口 UiTopbar_create/destroy/
setLed/setCount/setFps/setCpu；fps 域 0..999，cpu 0..100，
count 全 uint32。最新版 Figma 和实现均为 Montserrat 14，
本地字库实测最大计数宽 86、999FPS 宽 55、100% 宽 35；
相对 Figma 的 85/52/36 框，计数/FPS 向左扩 1/3 px，
保持页面右边界 101/200/238，三个标签全部右对齐、高 17。
左分组 99x17，LED 10x10 居中；右分组 93x17，FPS/CPU 间距 2。
LED 的 Figma y=5.5 在 LVGL 居中布局取 6，误差为半像素。
文本经 lv_label_set_text_static 借用实例内缓冲，
实例不得在存活期移动或复制。

widgets/page_title/page_title.c/.h 是被动页标题组件，
widgets/boot_count/boot_count.c/.h 是独立的计数分组组件
（均仅由 SysInfo 页经 ui_sys_info helper 挂载；Sm_Ui 分别
持有实例，SysInfo entry 建、exit 销）：PageTitle 是 2 个
LVGL 对象——236x17 普通容器（非 flex）+ 左侧 81x17 标题
标签（Montserrat 14，编译字体实测 81 px 宽，Figma 标注
79，组边界不变，借用调用方字符串、CLIP）；BootCount 是
3 个 LVGL 对象——151x17 flex 分组
（SPACE_EVENLY/CENTER，列距 2）装 52x17 的 "BOOTS" 标签和
97x17 计数标签（Montserrat 14，借用实例内 countText）。
编译字体实测 BOOTS 需 52 px，比 Figma 补宽 1 px；
uint32 上限需 97 px，比 Figma 的 95 px 补宽 2 px。
helper 把 BootCount 挂进 PageTitle 容器并右对齐，组合
共 5 个对象；文字框及组高均为 17，避免大字体裁剪。接口
UiPageTitle_create/destroy/setTitle 与
UiBootCount_create/destroy/setCount；setCount 把 uint32
格式成带千分位逗号的十进制
（上限 "4,294,967,295"；Figma 示例 2^32 是占位值）。
外观来自共享 const 样式 UiStyle_pageTitleText /
UiStyle_bootCountText。已挂载 SysInfo，进入时读取 NvmBoot 活块计数。

widgets/thread_monitor/ 包含 thread_monitor.c/.h 和
thread_card.c/.h。UiThreadCard_create/destroy 管一个
232x83 被动卡片容器，父容器由调用方传入，外部定位由调用方负责。
卡片内有 232x17 普通标题容器，复用 Label_create 创建两个标签：
左侧线程名 174x17、右侧优先级文字 58x17，分别内部 LEFT_MID /
RIGHT_MID 对齐，文字也分别左/右对齐；长文字 CLIP，不相互覆盖。
固定尺寸、对齐、字体和颜色来自 styles/ui_style.c 的
UiStyle_thread* 常量样式（Montserrat 14、白色），实例共享引用；
此处宽度分配是实现选择，不是新增 Figma 测量。
资源卡片的固定标签和内框、LiveShow 标签和开关也从同一文件
引用常量样式，不再为每个实例保存重复的局部属性表。
LabelConfig.style 非空时由该样式提供尺寸、对齐和外观；
width/height 须为 0、textAlign 须为 AUTO，不再创建局部样式。
未传 style 的旧配置保持原有行为。调用方仍须保证共享样式
和借用文字的存储期；卡片动画的动态位置与外框尺寸仍独立。
默认主题、24 KB LVGL 池和页面生命周期均保持不变。
默认文字为空，UiThreadCard_setHeader 借用两份调用方字符串，
不解析优先级、不采样；字符串须保持有效，原地修改后须再次调用。
卡片自身采用 COLUMN Flex、行距 10：标题行 y=0、高 17，
两条资源行分别 y=27/60、高 23。每行是普通容器，
内部 value 标签 174x17 左上对齐、unit 标签 58x17 右上对齐，
底部 Bar_create 创建 232x6 原生条，挂载 track/indicator 样式。
所有标签复用 Label_create；两条资源行不另建公共组件。
UiThreadCard_setResource 按 STACK/INBOX 行更新借用的 value/unit
文字及 0..100 进度；只更新指定行，不采样、不解析文字或计算占比，
进度使用 LV_ANIM_OFF。默认文字空、进度为 0，轨道可见。
UiThreadMonitor 持有两份 UiThreadCard，创建时挂到 232x83 的
裁剪 viewport 下，初始 x 分别为 0、232；外框为 236x105，
viewport 相对外框偏移 (2,6)。viewport 的兄弟节点是独立 UiDots
组件（widgets/dots/dots.c/.h），Monitor 仅持有 UiDots dots，
不保存圆点数组或自己创建各圆点。UiDots_create/destroy
负责整组生命周期；公开实例只有 container，各圆点是内部细节。
UiDots_create 接收非零 uint8_t count，组宽为 count * 12 - 6，
高度为 6，首点高亮。ThreadMonitor_create 额外接收 pageCount，
只改变分页点数，不改变预建卡片数。SysInfo 传入 5，组件为 54x6，
五颗 6x6 圆点，间距 6，
外框 COLUMN flex 横向居中使组落在相对 (91,99)。
第一颗挂 UiStyle_dotActive（#6FE97B）于共享
UiStyle_dot（#334155、CIRCLE、COVER）之上，外观不变。
UiDots_setActive 接收从零开始的索引，组件自身不定时或翻页。
销毁时先销两张卡和 Dots，再销外框。
实例须零初始化、仅在 UI 线程操作，destroy 可重复调用。
每卡 12 个 LVGL 对象，五点 Monitor 共 32 个；已由 SysInfo
helper 挂载，Sm_Ui 持有实例，进入时创建、退出时销毁，
不影响常驻 Topbar。

线程数据由 UI 线程适配器调用 vTaskGetInfo 采集，
HSM 负责格式化、绑定与翻页，Card/Monitor 保持被动。
固定顺序为 UI、Blinky、Flash、Idle、Timer daemon：

- 优先级为 FreeRTOS 当前优先级，显示 P 数值，不是 SST 逻辑优先级。
- 栈值为 min 最低剩余字节数／总字节数，Bar 表示剩余比例；
  总量从栈起止地址得到，FreeRTOS 水位从 words 转为 B。
- UI、Blinky、Flash、Timer 队列均显示 min 历史最低剩余／容量。
  AO 水位在 port 的 task/ISR 入队临界区更新，不依赖页面采样。
  Timer 水位通过 FreeRTOS 的 TmrQ 注册和成功发送 trace hook 更新，
  同时覆盖 task/ISR 命令发送，不修改第三方内核或 SST 源码。
- Idle 没有业务队列，显示 N/A；Timer 队列尚未注册时显示 --/10。
  未知或不适用时队列 Bar 降低透明度，不把 0 当成真实耗尽值。

SysInfo 进入时绑定首卡与备用卡，每秒刷新可见卡；
每 2 s 启动一次 240 ms 水平滑动，
旧卡向左退出，新卡从右进入；复用两个实例并同步五点高亮。
Topbar、标题及分页点不参与动画，viewport 裁剪卡片内容。
动画中忽略重复翻页事件，避免覆盖仍在显示的槽位数据。
退出页面先停轮播，Monitor 销毁时取消动画，再销毁控件；
动画无法启动时直接落到目标位置。暂停自动切页的设置保持不变。

水位回归：python3 tests/test_queue_watermarks.py。
覆盖 AO/Timer 在 task/ISR 入队产生的峰值，出队后水位不回升。

定位依据为最新 .fig 的 ThreadStackHighWater（125:64）：
页面内外框 (2,58.570732)、236x105，整数像素取 (2,59)；
viewport 相对外框 (2,6)，故页面坐标为 (4,65)、232x83。
Root 的页面 padding 提供 x=2；Topbar 高 17，PageTitle 顶部
margin=1，使标题落在 (2,30)，对应 Figma 的 (2,30.285)。
ThreadMonitor 保留顶部 margin=2，横向不再重复补边距；
继续参与 Flex，使卡槽和 viewport 的坐标保持不变。

[原 UML](uml/ui/ui-components.html)是清空前的历史快照。

运行 `python3 tests/ui_components/run.py` 进行 ASan/UBSan 宿主验证，
覆盖启动事件先后顺序、延迟自投递、缺 Splash、Splash 像素、空白页、
反复切页、局部/整屏重绘一致性和事件池回收。
清空版本尚未执行固件编译或上板验证。

## 下方资源卡片槽（已挂载 SysInfo）

widgets/resource_monitor/resource_monitor.c/.h 提供
UiResourceMonitor_create/destroy；持有裁剪 viewport、
两份 UiResourceCard、独立 indicatorContainer 和 UiDots，
不暴露各圆点指针。UiResourceCard 位于同目录 resource_card.c/.h。

外框 236x144，取自 Figma 下方 Frame 9（127:104）的外框尺寸
236x144.42926025390625。按 Figma event_pool_size 分区：
viewport 为 236x118、相对外框 (0,26)，关闭滚动和溢出可见。
两张 236x118 卡片只创建一次，初始坐标由局部 const lv_point_t
数组传入，分别为 (0,0)、(236,0)。每卡内部为纵向 Flex，
顶部留白 1、分区间距 4，包含标题区 236x27、
圆弧区 102x35 和范围区 236x17。
标题区包裹资源名与容量组；圆弧区包裹原生 lv_arc 和百分比；
范围区包裹观测值与总量。文字复用 Label_create 和共享 styles。
块大小组宽 43、数值标签宽 32，容纳三位块字节数；
底部两个标签宽 48，容纳 LVGL 的五位字节数，文字锚点不变。
圆弧完整对象为 100x100，背景角度 207..333、线宽 6，
白色轨道、绿色 #BEE086 指示条，无 knob；仅圆弧区允许溢出，
外层卡片与 viewport 仍裁剪。初始值 0，未知文字为 -- / --%。

indicatorContainer 为 viewport 的兄弟节点，
236x46、相对外框 (0,97)，按设计稿与下方范围区叠放。
五点 UiDots 为其子节点，54x6、相对 (91,16)，
不会跟卡片移动；包含 Big 预留项，点组保持居中。
每卡 12 个 LVGL 对象，Monitor 共 33 个。
五项轮播仍复用两张卡，不增加第三张资源卡。

实例须零初始化，父对象须存活，UI 线程独占，destroy 可重复调用。
已由 SysInfo helper 挂载（Sm_Ui 持有，entry 建、exit 销，
在 ThreadMonitor 之后创建、横向由 Root padding 定位）：挂载后外框
(2,174) 236x144，viewport (2,200) 236x118，卡片 x 2/238
y 200，圆弧 (70,238)，五点组 (93,287) 54x6。
已接真实采样、每秒刷新、2 s 轮播及 240 ms 水平滑动。
两张卡复用，旧卡向左退出、新卡从右进入，独立五点同步高亮。
上下槽共用 cardSlideTimeout，周期为 2U * BSP_TICKS_PER_SEC，
投递 UI_CARD_SLIDE_TIMEOUT_SIG，同时推进两组卡片。
退出页面停掉共用定时器，销毁 Monitor 时先取消动画；
动画中不接受重复翻页，不移动标题、Topbar 或圆点。
UiResourceCard_setHeader/setGauge 仅绑定调用方字符串和百分比，
字符串由 HSM 的双槽缓冲持有，Card 不采样、不计算比例。

固定顺序与口径：

- Small min free：小事件池最低剩余块数／总块数。
- Medium min free：中事件池最低剩余块数／总块数。
- Big reserved：大事件池预留位，未注册实际池；
  块大小、容量、水位与百分比显示 --，圆弧降低透明度。
- Tracer min free：Tracer 最低剩余块数／总块数。
- LVGL peak used：LVGL 分配器 max_used／total_size，单位为 B。
  表示历史峰值占用，不声称是最低可分配空间或最大连续空闲块。

Small、Medium、Tracer 从已有 nMin 水位读取，释放后不回升；
圆弧表示最低剩余比例。Big 仅预留展示项，不分配内存池，
不访问尚未注册的 poolId 3。ResourceSample.total 为 0 表示未绑定，
显示未知值，不作为健康的 0%；槽位重用后恢复正常圆弧透明度。
右上为实际块大小 B；LVGL 按 1 B 为显示单位，不代表分配器块大小。
底部左／右分别为观测值与总量，中央为对应比例。
LVGL 与三种最低剩余读数方向不同，peak used 明确表示峰值占用。
LVGL 最大连续空闲块及碎片率暂不增加额外字段。
线程槽与资源槽各自使用独立五点组。

## SysInfo 资源健康范围备忘（已确认）

本节记录后续实现范围，不表示采样或显示已经接入。
目标是一页覆盖系统资源健康，线程明细通过卡片轮播查看。
先确认信息归属，不在此确定新控件或采样接口。

- 上方线程卡片槽：各线程的栈水位、事件队列余量和优先级，
  包括 Timer daemon 等内核线程。无对应队列时显示不适用，
  不将其伪装成零余量；Timer daemon 的命令队列不是 AO 邮箱。
- 下方公共资源卡片槽：小事件池、中事件池、Big 预留位、
  Tracer 水位、LVGL 内存池，共五项。槽位风格沿用线程卡片槽，
  项目数不等于预建卡片数；分页指示应对应五项，
  与上方线程槽分别维护。
- CPU 负载和 UI 刷新率沿用 Topbar，不在资源卡片中重复展示。
- 本期不纳入 FreeRTOS heap：当前使用全静态分配，
  不为不存在的动态堆展示空闲量。
- 故障、复位原因和存储异常摘要暂不考虑，不扩大本期范围。
- 中断栈 MSP 可列为后续补充，不阻塞第一版；
  线程栈水位不代表中断栈水位。本备忘不引入主栈涂色、
  探针或早期启动动作。

数据口径约束：

- 当前剩余和历史最低剩余必须明确区分。
  栈高水位表示历史最低剩余；事件池接口的 min 也不是当前值。
- 小池和中池分别观察，不将不同块大小的容量合成一个比例。
- LVGL 内存池独立于应用事件池，不能用事件池余量替代。
  当前选择 LVGL 历史峰值占用，资源卡每秒刷新。
- 数据未知、不适用或过期时不能用正常的零值或旧值掩盖。
- 采样、换算与数据适配放在被动控件外。

待后续决定：轮播一圈与当前 SysInfo 停留时长如何协调；
本次备忘不修改页面时长，也不接入采样或动画。

以下为先前的排障结论和测量，保留作参考，不代表当前控件清单。

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

## LiveShow 圆弧和进度条

UiArc 是公共 Arc_create helper 之上的被动百分比组件：
UiArc_create / UiArc_setValue / UiArc_destroy。
取值契约为 0..100；setter 同步更新圆弧和内部百分比文字，
不静默截断超范围值。根对象就是 lv_arc，只有一个居中的 Label
子对象，不再包一层布局容器。270 度背景弧、10 px 线宽；
create 初始值为 0，外框宽高支持像素或 lv_pct()。
实例持有文字缓冲，活动期间不能复制/移动或外部删除子树。

UiLiveMotion 是 LiveShow 上栏的 Flex 组合：

```text
COLUMN（间距 7）
├── ROW（LIVE ACTIVITY、LED status 开关）
└── ROW（高 70）
    ├── UiArc（70x70）
    └── COLUMN（flex_grow）
        ├── INDICATOR RUN
        ├── 百分比
        └── Bar（宽 100%、高 6）
```

UiLiveMotion_setValue 将同一演示值传给 UiArc、数值标签和 Bar，
不采样硬件、不隐式启动动画。Bar 复用 Bar_create。
Motion 子状态以 TimeEvt 调用 setProgress，
统一驱动 Arc、Bar、文字和演示开关，不创建 lv_anim。
Motion 外框在 Root 下按 COLUMN 自然排列，宽 100%、高 112，
不设置绝对坐标。LiveShow entry 创建，exit 销毁；Topbar 常驻。
LED status 为被动 lv_switch，37x18，蓝色开启轨道、白色滑块。
UiLiveMotion_setLedStatus 只设置显示状态，不回调、不控制硬件；
create 默认关闭，LiveShow 按演示前后半周期切换，不绑定 LED 事件。
下栏 Frame 13 由 UiLiveSignal 创建，宽 100%、高 133。
内部 COLUMN 使用 SPACE_BETWEEN，依次为标题行、正弦线、相位条；
标题行横排 Signal / Phase 与 MOVE DEMO，使用 Montserrat 14，
文字框高 17。波形为 lv_line，101 个 const 参考点留在 Flash，
按正弦公式每 2 px 采样并取整，约 200x30 的一个周期；
实例持有可写的 101 点缓冲供 Line 借用，不能移动活动实例。
LVGL 整数线宽取 2，近似 Figma 的 1.5，不新增图片或 Chart。
五段相位条每段 20x4、间距 7，总宽 128；默认高亮第零段。
UiLiveSignal_setPhase 只选择 0..4 高亮，不移动波形或启动动画。
UiLiveSignal_setProgress 接收 0..99 的周期进度，同步平移波形
并更新五段相位。复用实例点表，不每帧申请内存或计算三角函数。
Signal 子状态以 100 ms 周期 TimeEvt 每次推进 2%。
UiLiveMotion_setProgress 接收 0..999 千分比周期进度，
同步更新 smoothstep 往返值与开关。
LiveShow 父状态创建两栏，初始进入 arcShow，5 秒后转 signalShow，
两者按 QM 双向切换，整页仍在 10 秒后离开。
两个子状态共用节拍与切换超时，entry 按当前状态 arm、exit disarm；
另一栏保留最后画面。切阶段不隐藏、删除或重建控件。
每帧复用对象、文字和点表；组件不拥有 lv_anim、lv_timer、TimeEvt，
只暴露被动 setter。两栏进度均按 5 秒一轮的速度推进，
Motion 实际 33 1/3 Hz（3 tick），Signal 为 10 Hz。
进入对应子状态从零开始，计时和阶段决策属于 HSM。
helpers/ui_live_show.c/.h 提供 UiLiveShow_build/hide，与 SysInfo
helper 对称。build 挂背景、标题、Motion 和 Signal；hide 逆序
销毁页面控件并清背景，不删除 Root、常驻 Topbar 或兄弟对象。
HSM 的 LiveShow entry/exit 调用该 helper；子状态切换、TimeEvt
启停与进度推进仍留在 HSM，不放进页面 helper。
按 2026-11-06 最新 Figma，上栏标题、圆弧百分比、右侧说明和数值
使用 Montserrat 14，文字框高 17；左右 padding 为 10，给长说明
留出空间。页标题与 Topbar 本来就是 14，保持现状。
下栏标签已按最新设计从 Montserrat 10 同步为 14。

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
