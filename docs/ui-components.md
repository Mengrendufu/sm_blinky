# UI 的能力边界与 UML 映射

本页是已落地代码的 subsystem/component 映射草案。权威 StarUML 模型
仍在外部，本轮只读导出，没有直接改写模型。模型中的 application
尚未展开 UI 内部能力。

## 能力与归属

```text
application <<package>>
└─ UI <<subsystem>>                         普通线程上的完整 UI 能力
   ├─ Runtime <<component>>                 收件箱、派发、定时节点接线
   ├─ Display Adapter <<component>>         LVGL heartbeat/flush、LCD 等待
   └─ State-driven Presentation <<subsystem>>
      ├─ HSM Orchestrator <<component>>     事件决策、状态与能力生命周期
      ├─ Runtime Observation <<component>> CPU、栈与资源水位快照
      └─ Presentation <<subsystem>>
         ├─ Background Surface <<component>> 容器与 RGB565 描述符
         ├─ Topbar <<component>>           标题、FPS、CPU、LED
         ├─ Sys Presentation <<subsystem>>
         │  ├─ Gauge <<component>>
         │  ├─ Resource Panel <<component>>
         │  └─ Page Dots <<component>>
         └─ Live Presentation <<subsystem>>
            ├─ Arc Sweep <<component>>
            ├─ LED Switch <<component>>
            ├─ NVM Counter <<component>>
            ├─ RAM Readout <<component>>
            └─ Page Dots <<component>>
```

Subsystem 表达组合能力，Component 承载叶子能力，Package 只组织依赖。
协调者调用组件 API，不在其内部拼装样式、采样内核或推进圆弧算法。
C 结构体为了静态分配而嵌入其他实例，不等于在 UML 的叶子 Component
内部嵌套 Component；上述能力均归 UI subsystem，协调者持有访问引用。

## 契约与代码承载

| 能力 | 提供的契约 | 代码 |
|---|---|---|
| Runtime | 事件入列、连接计时投递与收件箱 | thread/ui* |
| Display Adapter | 请求重绘、背光、完成帧率、显示推进 | thread/ui_disp* |
| HSM Orchestrator | 初始化、同步消费事件 | sm_ui* |
| Runtime Observation | 主动采样、只读值快照 | metrics/ui_metrics* |
| Background Surface | 显示 Splash/页背景、借出内容区及覆盖层父对象 | views/ui_surface* |
| Topbar | 创建/销毁、标题、FPS、CPU、LED | widgets/w_topbar* |
| Sys Presentation | 创建/销毁、呈现资源快照 | views/ui_sys_page* |
| Live Presentation | 创建/销毁、设置开关/计数、推进圆弧 | views/ui_live_page* |

HSM 所需 DisplayOperations 由线程实现并绑定；时间事件则直接依赖
common_c 的 TimeEvt，由 HSM 持有节点并直接 arm/disarm。公共计时
服务只借用不透明事件，投递适配端
负责将其恢复为静态 SST_Evt 并入列；不引入 UI 的 SST_Task 身份。
观测组件借用 BSP、FreeRTOS、AO 句柄、收件箱和池的水位查询能力，
向 HSM 提供只读快照；不调用显示 API。

## 状态与组件生命周期

| 状态 | 进入 | 消费的显示事件 | 退出 |
|---|---|---|---|
| TOP 初始化 | 初始化观测与显示实例、创建内容底面 | — | — |
| Splash | 显示已验证图片、arm 展示期限 | 两阶段就绪汇合 | disarm 展示期限 |
| Active | 创建公共顶部、采样、arm 50 ticks 周期刷新 | 公共刷新及 LED | disarm 刷新、销毁顶部 |
| Sys | 标题/背景、创建本页、arm 500 ticks | 数据更新后呈现快照、期限到达跳 Live | disarm、销毁本页控件 |
| Live | 标题/背景、创建本页、arm 1,000 ticks 及圆弧周期 | 圆弧步进、开关、计数、页超时跳 Sys | disarm、本页销毁 |

100 Hz 时基下，公共顶部 500 ms 更新，圆弧每 3 ticks（30 ms）推进
两个百分点，在空/满之间循环。每个页面自己消费页面超时，Active
不查询当前子状态决定跳转。readyShow 保持 Splash 父状态的同一次计时。

Active 消费 LED 事件，更新公共模型、边沿计数和顶部 LED，然后
自投递 UI_REFRESH_SIG。Live 消费已更新的数据，设置本页开关，读取
NVM 全局快照更新计数。Standby/Splash 保留 LED 模型，其间没有顶部
控件。Blinky 在 ON/OFF entry 更新 NVM 增量，页面不修改业务快照。

## 资源与数据显示

Surface 拥有内容容器及背景描述符，借用已经验证的 SRAM 图片。
Sys/Live 在状态互斥期间独占借用该容器，拥有其中的控件；destroy
只清理本页控件，不删除 Surface。顶部使用屏幕父对象，跨切页保留。
因此没有为了逻辑分组额外引入 LVGL 容器。

观测组件在 Active 的公共刷新事件中采样，再通过 UI_REFRESH_SIG
通知页面。Sys 呈现已更新的快照，按 revision 过滤重复绘制。
Splash 与页面期限共用一个节点，另保留公共刷新和圆弧两个节点；
取消每 10 ms 唤醒 UI 的固定轮询节点。
FPS/CPU 和 NVM 标签的重复值过滤分别由其显示组件负责。水位口径保持，
Sys 资源条共九行：五个任务栈、tracer、UI 收件箱、小池、中池。
smin/mmin 分别显示各池历史最低剩余块数与总块数，不再合并水位；
资源条以 18 px 行距排列，最末行 y=286。
CPU 保持实时窗口口径。圆弧进度与方向属于 Arc Sweep 组件，HSM 只决定
Live 期间何时推进一步。

## 验证与边界

本轮对实际项目头文件做语法检查，并用真实 LVGL、HSM、公共 TimeEvt、
收件箱、线程注册/派发适配及显示组件进行 ASan/UBSan 宿主集成验证。
BSP、RTOS 调度/统计、NOR 资产读取和 LCD flush 的外部作用由桩提供。
覆盖启动完成顺序、缺图与积压，公共顶部跨切页保留、本页销毁、
5 s/10 s 切页、圆弧空/满循环及退出后的旧步进事件、LED 委托与计数、
NVM 快照只读显示、水位快照及单独的内部到外部退出动作。

原生测试为 64 位对象布局，使用测试专用 64 KB LVGL 池；24 KB 的
宿主配置出现分配失败。固件仍使用原有 24 KB 配置。
宿主池占用不能当成 MCU 的 RAM 预算；本轮没有编译、烧录固件，
板端栈/堆余量、显示性能及真实画面仍需后续验证。

可重复运行的宿主测试入口：

```sh
python3 tests/ui_components/run.py
```

该入口只生成独立的原生构建与测试目录，不运行固件构建 preset。

本轮证据：`/tmp/ui-components-q3yp0hdt/`。其中 model-tree.txt 为模型
只读导出，native-results.json 为六种场景结果，PNG 为实际 LVGL
宿主渲染画面；native-build* 日志包含首轮失败和修正后的宿主验证。
