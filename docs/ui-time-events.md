# UI 显式时间事件

UI 直接使用 common_c 的 TimeEvt 服务（submodule 提交 777cf4f）。
公共模块只处理链表和计时，不依赖 SST 或 FreeRTOS。项目通过
`app/ports/common_c/time_evt_port.h` 提供临界区与 SM 断言适配。
`Application_tickStart` 将 SST 原有时间事件和通用 TimeEvt 分别挂到
BSP 的 100 Hz 时基回调表；BSP 不理解事件信号、投递方式或 HSM。

## 三个节点及生命周期

所有节点均由 Sm_Ui 持有，直接调用 TimeEvt_arm/disarm。

| 节点 | 信号 | 使用范围 | arm | disarm |
|---|---|---|---|---|
| stateTimeout | UI_STATE_TIMEOUT_SIG | Splash | 单次 150 ticks | Splash exit |
| 同上 | 同上 | Sys | 单次 500 ticks | Sys exit |
| 同上 | 同上 | Live | 单次 1,000 ticks | Live exit |
| headerTimeout | UI_HEADER_TIMEOUT_SIG | Active | 周期 50 ticks | Active exit |
| dialTimeout | UI_DIAL_TIMEOUT_SIG | Live | 周期 3 ticks | Live exit |

Splash、Sys、Live 的期限互斥，复用同一节点及不可变的静态事件；
各状态独立解释 UI_STATE_TIMEOUT_SIG。节点不重新绑定事件或投递目标。
100 Hz 下分别为 Splash 1.5 s、Sys 5 s、Live 10 s；缺图时 Splash
使用一拍降级。进入 readyShow 不重 arm，也不退出 Splash 父状态。
后台完成和展示期限仍按两个独立一次性事件汇合。

Active 每 500 ms 采样运行数据并更新顶部 CPU/FPS，跨页面保留计时；
Live 每 30 ms 推进圆弧，退出 Live 后取消。Sys/Live 自己处理期限
到达事件并跳转，由目标页 entry 重新 arm，父状态不查询当前页面。

## 注册与交付

Ui_start 先绑定收件箱，再调用 Sm_Ui_ctor 注册三个节点。构造函数
内部持有静态投递 operations，回调固定调用 Ui_post。
调度器启动前完成注册，节点
初始均未 arm；UI 线程进入后绑定显示能力，再执行 HSM 初始化。
构造不涉及 LVGL。节点、静态事件、operations 及 context 覆盖全部
计时和排队交付生命周期，不允许运行期增删或重新绑定节点。

TimeEvt 的单位是外部时基脉冲，delay 必须大于零；interval 为零是
单次，为正是周期。重新 arm 替换当前倒计时，disarm 不移除链表节点。
节点在临界区内提交到期事实、重装周期或停止，退出临界区后调用
投递回调。回调只通过 Ui_post 入列，不阻塞、不直接派发 HSM。
事件 poolId/refCtr 为零，按 SST 静态事件规则消费。

取消或重 arm 不能撤回已经提交的到期或已入列消息。当前启动链路
的 Splash 仅到期一次，进入 Active 后再将同一节点用于页面期限。
未加入代次、队列撤销或重新进入 Splash 的协议。
arm 后下一拍首次递减，存在一拍相位差；投递仍受 timer daemon
调度影响，业务处理仍需等待 UI 消费队列。

## 取消固定轮询事件

删除原先每 10 ms 投递 UI_TIMEOUT_SIG 的独立节点及信号。
外部 100 Hz 驱动仍运行，但不再每拍唤醒 UI。

Active 在采样或更新 LED 模型之后从池分配并自投递 UI_REFRESH_SIG：

- Sys 呈现更新后的运行快照，按 revision 过滤重复绘制。
- Live 根据已更新的 LED 模型设置开关，并合入尚未发布的边沿增量，
  更新 NVM 计数显示；进入 Live 时也合入 Standby/Splash/Sys 的增量。

自投递将数据更新与页面呈现按先后顺序连接，页面不提前读取旧数据，
也不承担公共模型的更新。公共信号仍与 UI 私有信号区间分开。
UI_REFRESH_SIG 没有对应计时节点，重复呈现不改变状态期限。
节点注册和期限管理不再经过 TimeoutOperations 或事件地址查找表。
组件职责见 [ui-components.md](ui-components.md)。

## 验证

计时服务宿主测试：

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-pie -no-pie \
  -Itests/time_evt_support -Itests/ports -Iapp/ports/common_c \
  -Iapp/ports/static_pool -I3rd_party/sm_sst/include \
  -I3rd_party/sm_hsm/include -I3rd_party/common_c/include \
  tests/test_time_evt.c 3rd_party/common_c/src/time_evt/time_evt.c \
  -o /tmp/test-time-evt
/tmp/test-time-evt
```

UI 集成测试使用真实 LVGL、HSM、TimeEvt、收件箱及显示组件：

```sh
python3 tests/ui_components/run.py
```

覆盖后台先完成、展示先超时、后台早于 GO、缺图、GO 延迟及 UI 暂停
消费期间超时。验证共享节点从 Splash 到页面期限的复用、5 s/10 s
切页、500 ms 采样后显示同一快照、LED 事件后的计数更新、圆弧循环
与退出后旧步进事件，以及批量呈现事件不触发采样或提前切页。
宿主使用测试专用 LVGL 内存池，不能据此确定 MCU 内存余量。

三个时间事件仍借用静态 SST 事件；GO 与 REFRESH 均从事件池分配，
经同一收件箱入列后派发、GC。宿主集成测试接入真实事件池与 GC，
结束时可重新分配两档的全部块，验证动态事件没有遗留引用。
