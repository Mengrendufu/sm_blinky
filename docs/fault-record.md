# 内核异常留存的有效性分类

故障入口先保存 no-init RAM 并复位。bootloader 初始化串口后，
在升级判断和握手窗口之前检查现场、持久化并输出 tracer 兼容帧。
本次发现有效 no-init 现场时，输出一轮后停留在 bootloader，
不进入资产烧写或跳转 App；对应分支预留后续 OTA 回滚触发点。
保存或串口输出失败也停留。无效 RAM 和 Flash 历史回放不触发停留。
持久化成功会清除 RAM 记录，因此再次手动复位可正常启动；
当前停留不跨复位锁存。
App 不再读取或清除该记录。资产烧写模式也输出诊断帧；上传端
先解 HDLC，再区分日志与应答。只有资产载荷窗口使用原始字节，
详见 `uart-link.md`。
这条通道描述 Cortex-M3 内核异常，与 LCD 验证失败计数相互独立。

## 冷上电误报的原因

旧实现把跨复位 RAM 中 PC、LR、CFSR 任一非零作为有效依据。
该区不随 C 启动清零，冷上电初值可能非零，因此稳定出现日志也不能
证明稳定触发了异常。示例 `PC=0x0AB2CDE0`、`CFSR-low16=0xD786`
不能定位真实故障：没有写入凭证，状态值还包含保留位。
历史日志本身无法补出真实性证据；须用新记录格式重新复现。

## 三类结果

| 分类 | 判据 | 输出 |
|---|---|---|
| EMPTY | 全部字段为零 | 直接返回，不查询 Flash、不输出现场 |
| VALID | 异常编号正确且完整性校验通过 | 输出现场和异常类型 |
| INVALID | 非零，但格式或校验不成立 | 报告忽略，再回放已有历史 |

VALID 表示故障入口完整写入过这个现场，不表示当前启动仍在故障中。
INVALID 不能解释成“确定没有异常”：冷 RAM、旧版格式、部分写入、
内存破坏都可能造成无效记录。真实故障若发生在坏 PC 或坏 LR，
地址不在正常代码区也应保留，因此不按地址范围排除有效现场。

## 存储与提交

两个镜像同处本仓库：`app/` 与 `bootloader/`。链接脚本各自保留，
共同预处理引用 `project_common/startup/project_layout.h`；记录格式与
保存汇编位于 `project_common/fault/`。RAM 顶部保留 160 B：

| 地址 | 大小 | 用途 |
|---|---|---|
| `0x2000FF60` | 128 B | Fault 记录区，实际记录 124 B |
| `0x2000FFE0` | 32 B | 原跨复位区，升级标志地址保持不变 |

app 和 bootloader 的初始 MSP 都改为 `0x2000FF60`，向下生长。
Fault 区为 `NOLOAD`，位于启动代码的 BSS 清零范围之外；链接断言
检查静态数据和最小堆栈保留是否越界。此次额外占用 128 B RAM。
**部署必须同时更新 bootloader 与 app**：旧 bootloader 的 MSP
仍从 `0x2000FFE0` 向下生长，会踩入新记录区。

记录保存 PC、LR、xPSR、R0–R12、入口 MSP/PSP、EXC_RETURN、
CONTROL、PRIMASK、BASEPRI、FAULTMASK、IPSR、CFSR、HFSR、
MMFAR、BFAR、SHCSR 和帧有效标志。MSP/PSP 是异常入口时的值，
并非直接等于异常前 SP；CONTROL 同样是 Handler 模式读取值。
地址故障寄存器必须结合 CFSR 的 MMARVALID/BFARVALID 解读。

`project_common/fault/bsp_fault_capture.S` 是四类 Fault 的统一入口。
整个保存、两次 LED 闪烁与 SYSRESETREQ 路径没有 push/pop、函数调用、
HAL、时基或串口依赖；先保存 R4–R11 和特殊寄存器，再考虑原栈帧。
遇到压栈/退栈错误、非标准 EXC_RETURN、栈地址不对齐或不在内部
SRAM 可读范围内时，不读取原帧；相应 PC/LR、R0–R3、R12、xPSR
置零，`flags=0`。否则 `flags=1`，表示已复制该帧，不能保证帧内容
未被此前的内存破坏改写。外部 SRAM 栈有意不在支持范围内。

故障入口先使 seal 无效，再写载荷，最后在内存屏障后提交 seal。
seal 使用格式种子 `0x46544C32` 覆盖全部 30 个载荷字，用于排除
随机初值和损坏数据，不是密码认证，也不能排除所有碰撞。
bootloader 使用非消费式 `BSP_faultRecordRead_()` 校验记录，
持久化成功或与最新完整记录完全相同时，才调用
`BSP_faultRecordClear_()`。Flash 保存失败时，现场保留在 RAM，
本次仍输出失败状态及现场并停留；下次复位再尝试。
串口失败不影响已经提交的持久化记录。

这不是断电持久化：只承诺正常保留 SRAM 的复位路径；掉电、SRAM
损坏、故障入口取指失败、内核 Lockup 或保存期间再次致命异常，
都可能使记录缺失或不完整。NMI/Reset 不接入此四类 Fault 入口。

## 片内 Flash 日志

| 区域 | 地址 | 大小 |
|---|---|---|
| Bootloader | `0x08000000` | 16 KB = 16,384 B |
| App | `0x08004000` | 492 KB = 503,808 B |
| 诊断页 0 | `0x0807F000` | 2 KB = 2,048 B |
| 诊断页 1 | `0x0807F800` | 2 KB = 2,048 B |

每条日志为 136 B：序号 4 B、完整现场 124 B、校验 4 B、提交标记
4 B。校验覆盖序号及全部现场，提交标记同时标识日志格式版本。
每页可放 15 条，最多保留 30 条；正常读取和串口回放不擦日志。

保存前逐字节比较最新有效记录的完整现场。完全相同返回 unchanged，
不解锁、不编程、不擦除；寄存器任一差异都会新建记录。
A → B → A 会保存三条，保留不同现场之间的变化顺序。
当前没有持久化重复次数，避免为计数重新写 Flash。

先填空白槽，脏的半条记录跳过。两个页都写满才擦旧页，最新记录
所在页始终保留；擦除后最多丢弃旧页的 15 条历史。新记录先写数据
与校验并逐半字回读，再最后写提交标记。启动扫描只接受提交标记、
完整现场 seal 与整条日志校验都成立的记录。

复位落在提交后、清 RAM 前时，下一次完整比较会跳过重复写入。
擦写失败保留 RAM 和旧的最新有效日志；无效 RAM 不写入 Flash。
不在 Fault 上下文擦写，HAL 时基和中断已在 bootloader 正常初始化。
同片 Flash 忙时可能阻塞取指，不能把 HAL 超时视为独立硬件看门狗。

两个镜像的链接范围均避开日志区，当前 J-Link loadfile 仅写镜像
实际占用的页。整片擦除、维修工具主动擦页仍会清掉日志。
固件升级后日志保留；现场尚未携带固件 build ID，异地诊断需要
另外保留对应版本的 ELF。RAM 提交到 Flash 之前掉电仍可能丢失。

## tracer 配对

| ID | 内容 |
|---|---|
| 0 | 固定保留：assertion，原协议不变 |
| 1 | 固定保留：保存结果、日志序号、现场字 0–13 |
| 2 | 固定保留：现场字 14–29 |
| 113 | `==BOOT==[fault-stash invalid; ignored]==`，不解码无效现场 |
| 242 / F2 | bootloader 文本日志；F0/F1 保留给控制命令/应答 |

协议配套更新外部 `sm_tracer/protocols/blinky_hmi.json`。
1/2 各包含 16 个大端 UINT32/HEX32 参数，载荷均为 64 B；按顺序
发送。1 的前两个字为保存结果和序号，随后按下面的现场字段顺序
携带全部 30 个字；seal 不输出。两条记录序号连续，第二条未收到时
现场不完整；两帧之间没有重传协议。
协议格式字符串使用显式换行，每行一个字段；两个帧仍各有 16 个
参数，无需 TUI 自动 wrap。修改显示排版不改变固件载荷。
旧 ID 66/67、110/111/112、114/115/116 退休，不再输出或复用。
原 ID 1 的 onStartup 占位定义已替换；应用未使用该占位记录。
用户看到 113 时，应先看作留存无效提示；只有 VALID 才输出 FAULT。
结果码：0=历史回放，1=新记录已保存，2=与最新现场完全相同，
3=保存失败。失败时序号为 0，输出的是仍待处理的 RAM 现场。
只有非空无效 RAM 的原有路径仍会回放最新历史；空 RAM 不回放。
回放不代表本次启动又发生了异常。

| 现场字索引 | 字段 |
|---|---|
| 0–5 | PC、LR、异常编号、CFSR、HFSR、BFAR |
| 6–11 | R0、R1、R2、R3、R12、xPSR |
| 12–19 | R4–R11 |
| 20–26 | MSP、PSP、EXC_RETURN、CONTROL、PRIMASK、BASEPRI、FAULTMASK |
| 27–29 | MMFAR、SHCSR、帧有效标志 |

bootloader 不依赖 App tracer 事件池，直接发送有界的串口帧；
单帧发送失败就停止本次现场输出。Flash 历史仍可由调试器读取，
目前没有新增串口遍历历史或清除日志的命令。

## 验证边界

```sh
python3 tests/fault_record/run.py
```

宿主 ASan/UBSan 测试覆盖冷 RAM、逐字节损坏、部分提交、四类
异常编号、坏 PC 保留、非消费读取、显式清除，以及 App 不再消费。

```sh
python3 tests/fault_log/run.py
```

真实日志与报告代码配合 Flash 模型，覆盖完整现场比较、同 PC 不同
寄存器、两页轮换、68 个半字写入中断点、擦除中断、提交后复位、
HAL 失败保留 RAM、串口失败不丢持久化记录。输出使用现有 HDLC
解析器验证全部寄存器、转义、校验及结果码。

隔离汇编验证依赖 `arm-none-eabi` 工具与 Python `unicorn`：

```sh
python3 tests/fault_record/run_asm.py
```

测试直接使用仓库内两个镜像的链接脚本和同一份公共汇编，
先用公共布局头预处理链接脚本，再进行隔离验证。
此测试仅预处理/汇编故障入口和最小桩，分别用两个真实链接脚本
检查地址与 NOLOAD 布局，再在 Cortex-M 模拟器中执行实际机器码。
覆盖四类 Fault、MSP/PSP、损坏 MSP、栈边界、未对齐、压栈/退栈
错误、无效 EXC_RETURN、写入边界、seal 最后提交、GPIO 闪烁路径和
复位请求；闪烁的等待循环被加速，不验证实际时长。模拟器输出的
真实记录再交给 C 消费端校验。模拟器不模拟真实异常入栈过程、
SRAM 电气故障或复位后的实际保留行为。

## 板端验证（2026-10-29）

临时 App 在 Blinky 活跃约 5 s 后执行 UDF #0。实际记录：
PC=0x08005260、exception=3、CFSR=0x00010000（UNDEFINSTR）、
HFSR=0x40000000（FORCED）、EXC_RETURN=0xFFFFFFFD、flags=1。
Bootloader 复位后成功提交第 1 条日志，清空 RAM，输出后停留。
J-Link 确认 PC 落在停留循环；用户确认完整现场输出及两次相同现场。
随后 RecID 1/2 版本上板，用户确认单列显示的全部字段。
临时注入已移除，正常 App 已恢复。

证据：`/tmp/boot-app-board-f1_86nuw/fault_final.jlink.cmd.log`；
后续阶段 HSM 版本的无标志握手、正常启动和日志区保留结果见
[uart-link.md](uart-link.md)。阶段重构后未重复注入 Fault；其停留
转移由宿主测试覆盖。真实掉电、Flash 写入中断及四类 Fault 的
板端逐项注入未在本次完成，不能用宿主模型替代这些覆盖。

## J-Link 离线读取

```text
savebin fault-journal.bin, 0x0807F000, 0x1000
```

片内记录为小端，UART 字段为大端。每页 15 槽，每槽 136 B：
sequence 位于偏移 0，124 B 现场位于偏移 4，checksum 位于 128，
commit 位于 132；页尾 8 B 未用。扫描时按页定位，跳过未提交或
校验无效项；不能把两页当作连续的 30 个槽跨页读取。
解释前应验证 commit、日志校验和与现场 seal；格式分别在
`bootloader/diagnostics/bl_fault_log.c` 和
`project_common/fault/bsp_fault_record.h`。日志未包含 build ID，
按 PC/LR 定位源码必须另留与故障版本匹配的 ELF。
