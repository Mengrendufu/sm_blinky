# UI 显式时间事件

普通线程使用 common_c 的 TimeEvt 服务（submodule 提交 777cf4f）。
公共模块只处理链表和计时，不依赖 SST 或 FreeRTOS。项目通过
`ports/common_c/time_evt_port.h` 提供临界区与 SM 断言适配；UI 持有
节点、静态 SST 事件并提供投递适配。BSP 只提供时基，SST 原有计时器
保持原样。
`Application_tickStart` 将两者分别挂到 BSP 时基回调表中，BSP 不理解
事件信号、投递方式或 HSM。UI 固定时基由周期节点产生
`UI_TIMEOUT_SIG`，不再单独挂载 Ui_tick。

```text
BSP timer daemon（100 Hz）
  ├─ SST_TimeEvt_tick：固定向 AO 投递
  └─ TimeEvt_tick：扫描静态链表，通过注册的 operations 投递
       ├─ UI 时基节点：每拍投递 UI_TIMEOUT_SIG
       ├─ Splash 节点：显式 arm，到期仅投递一次
       ├─ 页面跳转节点：每页进入时 arm，到期切页
       ├─ 公共顶部节点：Active 期间每 50 ticks 更新
       └─ 圆弧步进节点：Live 期间每 3 ticks 推进一步
```

## 节点与生命周期

`TimeEvt` 包含不透明事件指针、next、投递 operations、剩余脉冲数
和周期。HSM 定义 Splash、页面跳转的静态 `SST_Evt`，线程注册其
对应节点及固定 UI 时基节点。common_c 只借用事件地址；投递
适配将 `void const *` 转回 `SST_Evt const *` 再调用 Ui_post。
`TimeEvt_ctor` 绑定事件及投递能力，并将节点插入链表；不是 arm 时
再注册节点。节点、事件、operations 和 context 均需覆盖全部使用期。

调度器启动前完成一次构造与注册，不提供运行期增删链表。
`Ui_start` 先绑定收件箱，再注册 Splash、页面跳转、公共顶部和圆弧
四种超时节点，以及固定 UI 时基节点；随后以
`delay=1, interval=1` 启动周期时基，运行期不改变它的频率或使能。
这与现有 Blinky 在调度器启动前 arm SST 时间事件的顺序一致，且位于
BSP 的全部中断相关 bring-up 之后。UI 线程进入后，先向 HSM 绑定
所需的 arm/disarm 能力，再执行 HSM 初始化。

`TimeEvt_arm(node, delay, interval)` 的单位是 BSP 时基脉冲，delay
必须大于零；interval 为零表示单次，为正表示周期。重 arm 替换当前
倒计时。`TimeEvt_disarm` 返回此前是否仍在计时，不移除链表节点。

运行期计数的更新受临界区保护。tick 在临界区内提交到期事实、重装
周期或停止；退出临界区后调用投递 operations。回调只入列，不阻塞、
不调用 LVGL、不直接派发 HSM。

取消不能撤回已提交的到期，即使回调尚未入列；重新 arm 也不会撤回
旧事件。这里没有引入代次、队列撤销或取消后重入的新协议。

时间精度沿用 SST 的脉冲语义：arm 后的下一拍首次递减，存在一拍的
相位差。服务不受 UI 消费队列速度影响，但仍受 timer daemon 调度
影响；业务处理也仍需等待 UI 消费已投递的事件。

## HSM 定时策略与状态所有权

HSM 通过独立的 `Sm_Ui_TimeoutOperations` 按“事件地址＋ticks”操作：

```c
arm(context, event, ticks, interval);
disarm(context, event);
```

interval=0 为单次，正数为周期。只能使用预注册的静态事件地址。
线程将事件映射到预分配节点，未知
地址断言；运行期不改变注册关系。HSM 无需知道 TimeEvt 节点或投递
函数。到期经 Ui_post 入列，事件 poolId/refCtr 为零，按静态规则消费。

| 所属状态 | 事件 | 进入时 arm | 退出时 disarm |
|---|---|---|---|
| Splash 父状态 | UI_SPLASH_TIMEOUT_SIG | 150 ticks | 是 |
| Sys | UI_PAGE_TIMEOUT_SIG | 500 ticks | 是 |
| Live | UI_PAGE_TIMEOUT_SIG | 1,000 ticks | 是 |
| Active | UI_HEADER_TIMEOUT_SIG | 50 ticks，周期 | 是 |
| Live | UI_DIAL_TIMEOUT_SIG | 3 ticks，周期 | 是 |

应用固定时基为 100 Hz，对应 Splash 1.5 s、Sys 5 s、Live 10 s；缺图时
使用一拍降级。进入 readyShow 不会重 arm 或取消 Splash 计时，后台
完成和展示超时仍按原模型的两个独立一次性事件汇合。

每页创建控件后 arm 页面事件，退出时取消；Sys 收到页面超时跳 Live，
Live 收到页面超时跳 Sys，由目标页 entry 重新 arm。固定 UI_TIMEOUT_SIG 更新
当前页的快照/计数显示，不再承担顶部刷新、圆弧计时或页面跳转计数。
顶部刷新由 Active 消费专用事件，圆弧步进由 Live 消费专用事件。
删除 timeoutCount 及毫秒换算接口。组件职责见 [ui-components.md](ui-components.md)。
即使批量消费旧的固定时基事件，也不会提前结束当前页的展示时长。

## 验证

仓库内的计时服务宿主测试：

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-pie -no-pie \
  -Itests/time_evt_support -Itests/ports -Iports/common_c \
  -Iports/static_pool -I3rd_party/sm_sst/include \
  -I3rd_party/sm_hsm/include -I3rd_party/common_c/include \
  tests/test_time_evt.c 3rd_party/common_c/src/time_evt/time_evt.c \
  -o /tmp/test-time-evt
/tmp/test-time-evt
```

覆盖注册目标、单次/周期、重新 arm、取消、到期与取消的交错、静态
事件头和重复注册等契约。另以实际 UI 状态表/处理函数、线程适配函数、
收件箱及 HSM 内核验证六种顺序：后台先完成、展示先超时、后台早于
GO、缺图、arm 前积压的时基事件、UI 暂停消费期间超时。控件用桩。
验证每次 Splash 只 arm/到期一次，父状态退出时取消；使用实际
Sys/Live 的 entry/exit 验证分别于 500/1,000 ticks 到期并重新 arm，
并验证连续消费 1,000 条固定时基事件不会触发页面跳转。
本轮仅宿主执行与语法检查，没有编译或烧录固件。
