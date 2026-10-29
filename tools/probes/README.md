# J-Link 探针脚本

这里保留历史排查脚本，不是当前固件可直接复用的只读探针集合。
部分脚本会停核、写 LCD 命令/像素或触发复位：例如 `rw.jlink` 写
像素，`persist.jlink` 写面板寄存器，`sysreset.jlink` 写 AIRCR。
执行前必须阅读所选脚本、按当前 ELF 重新核对地址和副作用。
停核/恢复也会扰动运行；不能据脚本最后有 g 就称全过程无扰。
本轮实际验证脚本与转储位于系统临时目录，未覆盖这些历史文件。

## 怎么用

```sh
JLink.exe -device STM32F103ZE -if SWD -speed 4000 -autoconnect 1 -ExitOnError 1 -NoGui 1 \
          -CommandFile tools\probes\<名字>.jlink
```

在 WSL 里经 `cmd.exe` 调用即可 —— 写法与引号坑见 `tools/flash.sh` 的注释。

## 调查链（Probe #N，按当初的顺序）

| 文件 | 干了什么 |
|---|---|
| `gram.jlink` | 只读探针 #2：halt、读、继续。不复位、不写 flash |
| `rw.jlink` | 探针 #3 —— 决定性的那个：halt、写一个像素、读回、再恢复 |
| `baseline.jlink` | 探针 #4：健康状态的基线 + 连续性检查（也用来证明"接上调试器不会复位目标"） |
| `fingerprint.jlink` | 探针 #5：对控件区域取指纹，不是对空背景 |
| `failing.jlink` | 探针 #6：**坏的**那个状态 |
| `scan.jlink` | 探针 #7：坏状态下的全宽扫描 |
| `profile.jlink` | 探针 #8：x = 0..15 那一列的密集纵向剖面 |
| `edges.jlink` | 探针 #9：钉住噪声带的边界，再重复一段证明可复现 |
| `wide.jlink` | 探针 #10：跨一条噪声带和一条黑行的全宽 |
| `clear.jlink` | 探针 #11：噪声行能不能被写掉（把 176..183 行清成黑） |
| `persist.jlink` | 探针 #12：清过的行在固件跑了一阵之后还是干净的吗 |

## 单点读回 / 实验

| 文件 | 干了什么 |
|---|---|
| `probe.jlink` | 只读样板：复位原因（`RCC_CSR`）、SCB fault 状态、FSMC 时序寄存器 |
| `after_reset.jlink` | SYSRESETREQ 实验之后回读（164..199 行那条带） |
| `intermittent.jlink` | 偶发冷启动的回读 |
| `sysreset.jlink` | 判断软件方案（SYSRESETREQ）可用与否 |
| `ready.jlink` | 实验 B 回读：面板第一次接受填充是在哪个 tick |
| `curve.jlink` | 冷启动就绪曲线（密集轮询版本） |

## 度量

| 文件 | 干了什么 |
|---|---|
| `rate.jlink` | tick 速率：读计数器与决定它的两个量 |
| `ta.jlink` / `tb.jlink` | 读时序：内部 SRAM 1000 次读 vs 经 FSMC 窗口读面板 1000 次 |
| `stacks.jlink` | 任务栈高水位（按 FreeRTOS 的 0xA5 填充数未触碰的字，等价于 `uxTaskGetStackHighWaterMark`，不用 trace 通道） |
| `minfree.jlink` | 固件自己的最小空闲栈跟踪器 |
| `tmr.jlink` | timer 任务栈的分解 |
| `arcmeas.jlink` | 弧（arc）的临时测量回读 |

## ⚠️ 里面的地址是"当时那次构建"的快照

脚本里 `mem32 0x...` 用的绝对地址（任务栈、widget 数组等）随构建漂移。改了代码之后按符号重取：

```sh
arm-none-eabi-nm build/debug/sm_blinky.elf \
  | rg 'Ui_stack_|Blinky_stack|Flash_stack_|Ui_strip_'
```

**常用两行**：判"板子在不在跑" = 读两次 `xTickCount`（`baseline.jlink` 就是这么做的）；
判"串口会不会压住复位" = 开/关串口前后各读一次，看它是否还在增长。
