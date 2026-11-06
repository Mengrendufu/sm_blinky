# UI 显式时间事件

UI 直接使用 common_c 的 TimeEvt 服务（submodule 提交 777cf4f）。
公共模块只处理链表和计时，不依赖 SST 或 FreeRTOS。项目通过
`app/ports/common_c/time_evt_port.h` 提供临界区与 SM 断言适配。
`Application_tickStart` 将 SST 原有时间事件和通用 TimeEvt 分别挂到
BSP 的 100 Hz 时基回调表；BSP 不理解事件信号、投递方式或 HSM。

## 六个节点及生命周期

所有节点均由 Sm_Ui 持有，直接调用 TimeEvt_arm/disarm。

| 节点 | 信号 | 使用范围 | arm | disarm |
|---|---|---|---|---|
| splashTimeout | SPLASH_TIMEOUT_SIG | Splash | 单次 1.5 s | Splash exit |
| pageSwitchTimeout | UI_PAGE_SWITCH_TIMEOUT_SIG | Sys | 单次 12 s | Sys exit |
| 同上 | 同上 | Live | 单次 10 s | Live exit |
| statsTimeout | UI_STATS_TIMEOUT_SIG | Active | 周期 1 s | 无生产退出路径 |
| cardSlideTimeout | UI_CARD_SLIDE_TIMEOUT_SIG | Sys | 周期 2 s | Sys exit |
| animationTrigger | UI_ANIMATION_TRIGGER_TIMEOUT_SIG | Arc | 3 tick | exit |
| 同上 | 同上 | Signal | 10 tick | exit |
| animationSwitchTimeout | UI_ANIMATION_SWITCH_TIMEOUT_SIG | 两子态 | 5 s | exit |

Splash 与页面期限使用不同节点和不同静态事件；
Sys、Live 只复用页面切换节点。节点不重新绑定事件或投递目标。
arm 参数用 BSP_TICKS_PER_SEC 表达时长；缺图时 Splash
使用一拍降级。进入 readyShow 不重 arm，也不退出 Splash 父状态。
后台完成和展示期限仍按两个独立一次性事件汇合。

Active 每秒更新顶部 CPU/FPS，跨页面保留计时；Sys 每两秒同时推进
线程和资源卡片。Sys/Live 自己处理页面切换事件，由目标页 entry
重新 arm，父状态不查询当前页面。

## LiveShow 标准 QM 映射

以外部 hsm/application/ui/ui.c 为准，模型只读，不在仓库复制。

```text
active
└── liveShow：建页、销页，页面期限 10 s
    ├── arcShow（初始）：仅更新上栏
    └── signalShow：仅更新下栏
```

liveShow entry 一次创建两栏，不启动 lv_anim，initial 进入 arcShow。
两个子状态共用 animationTrigger 和 animationSwitchTimeout，
构造时各绑定一个静态事件，运行期只修改周期，不重绑信号。
arcShow entry 将 Motion 进度归零，启动 3 tick 节拍和
5 秒切换超时。3 tick 在 100 Hz 下实际为 33 1/3 Hz；
每次将千分比进度加 6，以 smoothstep 往返值更新 Arc/Bar/文字，
并更新演示开关。Signal 此时保持静态。

UI_ANIMATION_SWITCH_TIMEOUT_SIG 在两个子状态中双向转移：
arcShow -> signalShow，signalShow -> arcShow。
每个子状态 exit 停止两只共享节点，entry 再按本状态节拍重装。
signalShow entry 将下栏归零，以 10 Hz 信号每次推进 2%，
并重装 5 秒切换超时。非活动栏保持最后值；切阶段不销控件、
不换背景、不重 arm 页面期限。页面超时由父状态处理，无论当前在哪个
子状态，均先退出子状态停止节拍，再退出父状态销毁两栏。
父页面期限仍为 10 秒，因此正常流程到第二个切换点即离页；
模型的 Signal -> Arc 回转在页面尚未超时时同样有效。
已入列事件无法撤回：共享触发由消费时的子状态解释，
切换信号每次消费都会转到另一子状态，不再承诺重复切换去重。
页面外收到这两个信号则不处理。

此次恢复按消费事件数改值，队列延迟会造成追帧，不再使用
LVGL elapsed-time 插值。组件保持被动 setter，只有 HSM 管定时器。
保留 Splash、页面切换、统计采样、卡片轮播四个业务节点，
另加一只共享动画节点和一只切换超时，共六个节点。
卡片内部 LVGL 动画、Blinky 心跳/NVM 时间事件、100 Hz 公共时基
以及 LVGL 刷新定时器均保持不变。默认刷新周期仍为 33 ms。

## 注册与交付

Ui_start 先绑定收件箱，再调用 Sm_Ui_ctor 注册六个节点。构造函数
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
的 Splash 仅到期一次，其消息进入 Active 后不会触发页面切换。
未加入代次、队列撤销或重新进入 Splash 的协议。
arm 后下一拍首次递减，存在一拍相位差；投递仍受 timer daemon
调度影响，业务处理仍需等待 UI 消费队列。

## 取消固定轮询事件

删除原先每 10 ms 投递 UI_TIMEOUT_SIG 的独立节点及信号。
外部 100 Hz 驱动仍运行，但不再每拍唤醒 UI。

Active 直接处理 LED 事件和 UI_STATS_TIMEOUT_SIG，更新公共 Topbar。
Sys 在 UI_STATS_TIMEOUT_SIG 分支刷新卡片后向 Active 委托。
UI 只读 NVM RAM 数据，读取时由调用方持有 NVM_PORT 临界段；
显示和存储 IO 不在该临界段中执行。公共信号仍与私有区间分开。
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
消费期间超时。验证 Splash 与页面信号隔离、12 s/10 s 切页、
每秒采样、LED 事件后的计数更新、2 s 双卡轮播及退出动画清理，
以及 5 s 子状态切换、整页 10 s 期限不被重置、两栏互斥改值、
完整事件周期、双向切换、共享触发按当前子状态处理、
离页后的迟到事件、阶段切换对象身份
与点表地址不变，以及两个子状态分别提前离页时的计时器清理。
仅推进 LVGL 时间不改变演示值；没有两条页内 lv_anim 残留。
宿主使用测试专用 LVGL 内存池，不能据此确定 MCU 内存余量。

六个时间事件均借用静态 SST 事件；GO 与 LED 从事件池分配，
经同一收件箱入列后派发、GC。宿主集成测试接入真实事件池与 GC，
结束时可重新分配两档的全部块，验证动态事件没有遗留引用。
