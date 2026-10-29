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
Live 直接更新 Switch，并读取 Nvm_blinky 设置计数标签。
LED 事件及业务快照的原有归属不变，页面不修改 NVM 数据。

## 适配与资源

DisplayOperations 仍由 UI 线程实现并绑定，提供请求重绘、背光和 FPS。
时间事件仍由 HSM 直接持有并 arm/disarm；普通 UI 线程没有 SST_Task。
所有组件调用均发生在 UI 线程，符合 LV_OS_NONE 的单线程约束。

Sys 保留九行水位：五个任务栈、tracer、UI 收件箱、小池、中池。
CPU 保持实时窗口口径。布局、背景、资源采样和显示适配路径保持。

## 验证

运行 `python3 tests/ui_components/run.py`，使用真实 LVGL/HSM/TimeEvt
和收件箱进行 ASan/UBSan 宿主机验证。覆盖启动事件顺序、缺图、积压、
顶部跨页面保留、反复切页、圆弧往返及晚到超时、LED 与 NVM 显示。
退出检查验证各组件句柄清空，并确认同父容器的其他对象不被删除。

宿主测试使用 64 KB LVGL 池以容纳 64 位指针，不能代表 MCU 内存预算。
固件仍使用原有 24 KB LVGL 池。2026-10-29 正常固件编译烧录后，
板端确认进入 Live、资产有效和 NVM 恢复；用户确认画面、自动切页
和圆弧正常。此结果不代替最坏路径的栈或 LVGL 内存峰值测量。
