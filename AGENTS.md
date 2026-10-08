# AGENTS.md

STM32F103ZET6 固件骨架：FreeRTOS 内核 + `sm_sst` 的 AO/事件层 + HMI 外设口。
结构参考 `sm_tracer_tui`，板级参数参考已点亮的 Cube 工程 `free_rtos_blinky`。
不用 Keil，不用 STM32CubeMX。

## 构建：WSL 原生工具链

项目已迁到 WSL 原生盘（2026-10-02 从 `/mnt/c` 迁出），在仓库根目录直接构建：

```sh
cmake --preset debug && cmake --build --preset build-debug
```

- 优化：`toolchain_arm.cmake` 里显式带 `-O2`。Debug 构建默认是 -O0，而 LVGL 的软件渲染在 -O0 下慢到肉眼可见（实测一次局部刷新 28 ms，其中渲染 21~25 ms、我们自己的刷屏循环 4 ms），加 -O2 后 text 也从 257 KB 降到 166 KB。调渲染相关的行为时别把这一条去掉。（`-O3` / `-flto` 已评估、暂不采用，量级与理由见 `docs/charter.md` §6）
- WSL 依赖：`arm-none-eabi-gcc` 13.2.1（`/usr/bin`）、`cmake` 3.28.3、`ninja` 1.11.1。
- 产物：`build/debug/sm_blinky.elf`、`.hex`、`.map`。`build/` 是本机生成，勿跨平台复用。

## Git

项目在 WSL 原生盘上，直接用 WSL 的 git，毫秒级。2026-10-02 之前项目在 `/mnt/c`：那时遍历工作树的 git 命令要穿 9p、逐个进 submodule 比元数据，实测 WSL 侧 `git status --porcelain` 要 96.3 s，而经 cmd.exe 的 win32 git 只要 0.21 s，所以当时规定 git 走 Windows 侧。迁盘后这条规定作废。

## 测试：宿主机契约测试

`tests/` 下的测试**尚未接入 CMake/CTest**，需手工调用。都从仓库根目录执行，产物写 `/tmp`：

```sh
# 事件池 GC 契约（poolId / refCtr / 静态事件不回收）
gcc -std=c11 -Itests/ports -I3rd_party/sm_sst/include -Iports/static_pool -I3rd_party/common_c/include tests/test_sst_evt_pool.c ports/static_pool/sst_evt_pool.c 3rd_party/common_c/src/static_pool/static_pool.c -o /tmp/t_evt_pool && /tmp/t_evt_pool

# configASSERT 转发到 SM_onAssert（模块名 / 行号）
gcc -std=c11 -Ibsp/free_rtos -I3rd_party/sm_hsm/include tests/test_freertos_assert.c -o /tmp/t_assert && /tmp/t_assert

# 复合优先级 0xRRSS 布局的编译期契约（低字节 SST，高字节 FreeRTOS）
arm-none-eabi-gcc -std=c11 -DSTM32F103xE -DUSE_HAL_DRIVER -Iports -Iports/static_pool -Ibsp/free_rtos -Ibsp/platform -I3rd_party/sm_sst/include -I3rd_party/sm_hsm/include -I3rd_party/common_c/include -I3rd_party/FreeRTOS-Kernel/include -I3rd_party/FreeRTOS-Kernel/portable/GCC/ARM_CM3 -I3rd_party/stm32f1xx-hal-driver/Inc -I3rd_party/cmsis-core/CMSIS/Core/Include -I3rd_party/cmsis-device-f1/Include -c tests/test_sst_priority.c -o /tmp/t_prio.o
```

注意：

- 事件池测试**不要**链 `3rd_party/sm_sst/sst_c/src/sst.c`。`sst.c` 里未用到的 `SST_Task_run`/`SST_Task_post` 会拖入 `tests/ports/sst_port.h` 没有实现的 port 符号（`SST_start`、`SST_onIdle`、`SST_Task_setPrio`、`SST_PORT_TASK_PEND`）而链接失败。
- WSL 的 gcc 13 只把 implicit-function-declaration 当 warning（旧 MSYS2 gcc 16 当错误）。真要链 `sst.c` 时仍建议加 `-Wno-error=implicit-function-declaration` 并补 port 桩。
- 测试可执行文件写到 `/tmp`，不要落进仓库。

## 烧录：J-Link Commander

`tools/flash.sh` 一条命令搞定：编译 debug preset → SWD 烧录 → 复位运行。WSL 里从**任意目录**都能调，路径按脚本自身位置解析。

```sh
tools/flash.sh
tools/flash.sh -nobuild
```

它内部就是下面这两条，要手调参数时直接用：

```sh
cmake --preset debug && cmake --build --preset build-debug
/mnt/c/Windows/System32/cmd.exe /c "C:\mengrendufu\software_ins\JLink\JLink.exe -device STM32F103ZE -if SWD -speed 4000 -autoconnect 1 -ExitOnError 1 -NoGui 1 -CommandFile \\wsl.localhost\Ubuntu-24.04\home\sunnymatato\projects\sm_repos\embedded\hmi\sm_blinky\tools\flash.jlink"
```

- Commander 在 `C:\mengrendufu\software_ins\JLink\JLink.exe`（V6.86）。加不加 PATH 都能跑：加了能简写成 `JLink.exe`，不加就写全路径（脚本里就是全路径）。
- J-Link 是 Windows 程序而产物在 WSL，所以 `tools/flash.jlink` 里的 `loadfile` 走 UNC 直读 WSL 树：`\\wsl.localhost\Ubuntu-24.04\...`（已实测可读；换发行版名要同步改）。
- `tools/flash.jlink`：**只放连上目标之后的动作**（`r`/`h`/`loadfile`/`r`/`g`/`qc`），连接参数留在命令行，避免同一个事实写两处。一行一条命令，支持 `//` 注释；`loadfile` 自带擦除与校验，不用先 `erase`。
- 本版（V6.86）`loadfile` 的支持格式只有 `bin/mot/hex/srec`，所以烧 `build/debug/sm_blinky.hex`；`.elf` 留给 GDB/Keil。
- 失败判断：脚本以非 0 退出并打印 `[flash] FAILED`；`ERRORLEVEL` 非 0 来自 `-ExitOnError 1`。
- 从 WSL 调 `cmd.exe` 时它会打印 UNC 与路径相关的中文提示（当前目录是 UNC），可忽略。
- 旧的 `tools/flash.cmd`（Windows 侧入口）保留但已不用。
- 会话里敲 `?` 就是完整命令表。日常够用的：`st`、`Regs`、`mem32 68000000, 8`、`SetBP`/`ClrBP`、`g`、`r`、`qc`。
- 别用管道或重定向喂 `JLink.exe`（`JLink.exe < /dev/null` 之类）：它会在 `J-Link>` 空转，并且一直占着探针句柄，Keil 那边会连不上。跑完也可能残留 `JLink.exe`/`JLinkGUIServer.exe`：`tasklist | findstr /I jlink` 查一下，占了就 `taskkill /F /PID <pid>`。

## 目录与依赖

| 目录 | 内容 |
|---|---|
| `application/` | `main.c`、`application.c`（AO 启动）、`aos/blinky/`（唯一的 AO）、`ui/`（UI 线程：`thread/` 线程与收件箱、`hsm/` 状态机；不是 AO） |
| `bsp/` | 根下只有 `bsp.[ch]`：板级基础（时钟/DWT、LED 引脚与开关、`SM_onAssert`、tracer 出口与构造、`BSP_memReport`） |
| `bsp/devices/` | FSMC 上的两颗器件：`bsp_lcd`（ST7789，总线 + 面板 bring-up）、`bsp_sram`（IS62WV51216，总线初始化） |
| `bsp/free_rtos/` | FreeRTOS：`FreeRTOSConfig.h` 与 `freertos_hooks.c`（idle/timer 静态内存、栈溢出钩子） |
| `bsp/lvgl/` | LVGL 配置（`lv_conf.h`）；将来显示适配层也放这里 |
| `bsp/platform/` | 平台样板：HAL conf、MSP、TIM1 时基、中断向量、newlib 桩（`syscalls`/`sysmem`）、链接脚本。原来的 `main.h` 已删除，LED 引脚宏移到 `bsp.h` |
| `ports/` | SST 骑 FreeRTOS 的口；`ports/static_pool/` 是事件池适配 |
| `tests/` | 宿主机契约测试，未接 CMake |
| `tools/` | 烧录脚本（`flash.sh`/`flash.jlink`）与 `check_line_width.py` |
| `3rd_party/` | 7 个 submodule，全部以云端仓库为准 |

依赖：FreeRTOS-Kernel、common_c、sm_hsm、sm_sst、CMSIS Core 5.4.0、CMSIS Device F1 v4.3.3、STM32F1 HAL v1.1.8。
第三方统一放 `3rd_party/`，不要新建 `vendor/` 或 `third_party/`。

## UML 模型：不在仓库

架构模型用 StarUML，而 StarUML 是 Windows 程序、**读不了 WSL 原生路径**，所以模型不放仓库。权威副本：

```
C:\mengrendufu\workshop\umls\staruml\embedded\hmi\architecture.mdj
```

WSL 侧的读法：`/mnt/c/mengrendufu/workshop/umls/staruml/embedded/hmi/architecture.mdj`

**仓库里只放文档**（`docs/charter.md`）。2026-09-18 曾在 `docs/` 放过一份 `.mdj` 副本，16 天后就落后一版（`bsp` 那次改动没同步进来），已删除——**副本必然漂，所以不放**。

读模型走 uml-sync 技能，不要手解 `.mdj` 的 JSON：

```sh
python3 ~/.config/opencode/skills/sm/uml-sync/scripts/export_model_tree.py \
  --mdj /mnt/c/mengrendufu/workshop/umls/staruml/embedded/hmi/architecture.mdj \
  -o /tmp/model-tree.txt
```

## 改动约定

- **列宽 78，管的是行不是文件**：我们自己新增/修改的行不超过 78 **显示列**（CJK 字符按两列算，不是字节数）。模板与第三方原文——`bsp/platform/` 的 ST 80 列横线和许可文本、`bsp/free_rtos/FreeRTOSConfig.h` 的 Amazon 模板文字——保持原样；即使我们就在这些文件里改代码，也只约束我们加的那几行，原有超长行一律不动。检查：`python3 tools/check_line_width.py` 默认只量工作区相对 HEAD 新增/改写的行，外加未跟踪新文件整份；点名文件时整份量；非 0 退出即超标。`.editorconfig` 登记同一条规则，只有 `3rd_party/` 镜像与 `tools/flash.jlink` 的不可断行绝对路径豁免。
- **行尾不做统一**：树里 CRLF 与 LF 是混的（历史编辑 + 补丁插行造成的），这是知情保留的状态——不要为了"整齐"把某个文件整份转行尾，那会产生一整个文件级的 diff，把真实改动淹掉。`.editorconfig` 因此也没有钉 `end_of_line`。
- **尺寸单位**：本项目按 ST 链接脚本的口径，**KB = 1024 B**（`512KBytes FLASH` / `64KBytes RAM` 对应的就是 `0x80000` / `0x10000`），不用 KiB —— 引 ST 文档时两套写法会打架。会进预算表或要跟别人对齐的尺寸，旁边写上字节数（如 `15 KB = 15,360 B`）；十进制 KB（1000）出现在存储容量、链路速率那一类语境里，和内存尺寸不是一回事。
- **固件编译与烧录只在用户明确要求时执行**（说"编译"或"烧录"）。平时只做只读检查、读代码、跑宿主机契约测试；真要编/烧时，先说明跑哪条命令、烧哪份产物（时间戳 + 大小）。
- **FreeRTOS 走全静态分配**（`configSUPPORT_DYNAMIC_ALLOCATION 0`，不编 `heap_4.c`）。任务栈由各 AO 自己声明（`StaticTask_t` + `StackType_t[]`，见 `blinky.c`），idle/timer 任务的内存由 `bsp/free_rtos/freertos_hooks.c` 提供；新增 AO 必须自带静态栈。栈够不够用 `[MEM][STACK]` 记录（`BSP_memReport`）的高水位来定，不靠猜。
- **绑定先于派发**：新建或改动「环 + 唤醒目标」形态的通道时，`xxx_start()` 必须先把环与唤醒目标一起成型，之后才允许任何生产者投递（含 AO 的初始转移）；未绑定时的投递是静默丢唤醒，不是安全操作。机制与证据见「启动顺序」条。**待落地**（顺序已保证，代码尚未强制）：① 未绑定时的投递仍是静默接受（`ui_evt.c:49-55`、`flash_req.c:66-73`），应改成 fail-stop 或至少 trace 留痕；② flash 的「被拒」（`Flash_submit_` 返回 false）与「执行失败」（`req.ok` 为 false）仍压成一个返回值（`flash_req.c:137`、`149`），而唯一调用点 `(void)Flash_probe()`（`blinky.c:157`）把它丢掉；③ UI 环满仍「拒收最新」（`ui_evt.c:38-46`）——状态语义的通道应改成覆盖最新，或改成状态发布（`Sm_Ui` 这个「普通线程 + 内嵌 HSM」示范件保不保留未定）；④ UI 首帧按初值绘制、早于第一次排空（`ui.c:75`）。
- 依赖以云端 submodule 为准；本地拷贝只作对照。适配远端时允许改 `ports/`、CMake、BSP，但保留 `free_rtos_blinky` 已验证的板级参数。
- 要改 `sm_sst` 时：先在本地镜像 `C:\mengrendufu\workshop\local_prjs\sm_sst` 改并推送，再回本仓库同步 submodule 指针。
- 优先级编码 `0xRRSS`：低字节是 SST 逻辑优先级（用 `SST_MAX_TASK` 校验），高字节是 FreeRTOS 任务优先级（用 `configMAX_PRIORITIES` 校验）。契约检查留在 SST 核心公共入口，port 只额外校验高字节。
- 断言输出允许丢日志，但不允许卡死；复位优先于打印。

## 当前状态与已知坑

- **AO 的事件处理里不能跑长活儿**：SST 的定时事件是**按周期无条件投递**的（`sst_c/src/sst.c:157` 的 `SST_TimeEvt_tick` → `SST_Task_post`），而每个 AO 的队列只有 8 格（Blinky = `Blinky_qSto[8]`）。只要 handler 比 beat 慢，事件就堆积；满 8 格后 `SST_Task_post` 的 `DBC_REQUIRE(300, me->nUsed <= me->end)`（`sst.c:87`）断言、板子复位——日志上是 `[ASSERTION][+0000000300][sst]`。**「按 beat 切片」救不了**（试过 8 扇区/拍，实测一拍 1.28 s > 500 ms beat，照样堆），`vTaskDelay` 也救不了。正确做法是**把长活儿放进独立任务**：普通 FreeRTOS 任务没有这个队列、可以阻塞，给足静态栈 + 低于 AO 的优先级即可——flash 自检就是这么改的（`BSP_flashSelfTestStart()` 建 `fwt` 任务，优先级 20、栈 256 words）。
- 实测值：**这颗 flash 的扇区擦除约 160 ms**（自检期间 LED 间隔 1280 ms ÷ 8 扇区反推），比手册 45 ms 典型慢不少；整片 4096 扇区因此要 **≈11 分钟**。
- **栈溢出钩子会报出任务名**：`vApplicationStackOverflowHook` 现在把 `pcTaskName` 当 module 串传给 `SM_onAssert`，所以记录是 `[ASSERTION][+0000000001][ao]` 这种，一眼能看出是谁的栈爆了（`configCHECK_FOR_STACK_OVERFLOW 2`）。
- UI 线程（`application/ui/`）：普通 FreeRTOS 任务 + 内嵌 HSM，不是 SST AO。静态 TCB + 2048 words 栈（8 KB）、优先级 23（低于 Blinky 的 24）；收件箱是自建静态环（深 16）+ 临界区，投递满则返回 false 且**不阻塞**，投递成功用 `xTaskNotifyGive` 唤醒；主循环是 `ulTaskNotifyTake(wait)` → 排空收件箱 → `Sm_Ui_dispatch` → `Ui_dispRender()` → `Ui_dispHeartbeat()`；`wait` 来自 `lv_timer_handler()` 的返回值，在适配器里夹在 1 tick 与 10 ms 之间。Blinky 的 LED 跳变经 `Ui_postLedOn/Off()` 投递，UI 每 8 次跳变报一次 `[MEM][STACK][UI]`（RecID 24）。任务名 “ui”；SST port 建的任务名已从 “SST” 改成 “ao”。**已上板验证**（`[MEM][STACK][UI]` 正常出数，LED 心跳与既有记录不受影响）。增量 3：LVGL v9 在 **UI 线程内** bring-up（`thread/ui_disp.c`：`lv_init` → `lv_tick_set_cb(xTaskGetTickCount)` → `lv_display_create(240,320)` → PARTIAL 单缓冲 240×32 = 15,360 B 内部 SRAM → flush 同步写 FSMC 窗口）；demo 屏是标题 + 跟 LED 的方块 + edges 计数，由 Blinky 的 `Ui_post*` 事件驱动；分工是 HSM 只存状态（`ledOn`/`ledEdges`）、线程负责绘制，状态没变就什么都不碰。**未上板验证**。LVGL 裁剪：`bsp/lvgl/lv_conf.h` 关掉了没用的控件（chart/scale/table/dropdown/keyboard/textarea/arc/slider/switch/…）、布局引擎（flex/grid）、应用级机制（observer/obj id/font manager/translation/…）以及除 RGB565+A8 外的全部 draw 格式——LVGL 从 404 KB 降到 193 KB，镜像 text 475 KB → **255.6 KB**（49% of 512 KB；这是 LVGL 裁剪那次的对比，当前值见 `docs/charter.md` §6）。需要某个控件时把那行改回 1 即可；若 LVGL 断言"颜色格式不支持"，先开回 `LV_DRAW_SW_SUPPORT_ARGB8888`。**踩过的坑：UI 栈 2 KB 装不下 LVGL 的渲染调用链**——第一次带 LVGL 上板时整机冻死（灯不闪、屏不动），原因是栈溢出踩坏内存后触发硬故障，而当时 `HardFault_Handler` 是裸 `while(1)`，硬故障优先级高于所有任务，于是冻住而不是复位；栈抬到 4 KB 后正常；实测高水位：4 KB 下 `[MEM][STACK][UI]` 只剩 110 words（440 B），即最深处用掉约 914 words，512 words 的老栈是**缺了约 400 words** 才溢出的——这条数字就是定案的证据，所以栈现定 **2048 words（8 KB）**，对齐 LVGL 官方建议（> 2 KB、建议 > 8 KB）；LVGL 池也从 16 KB 抬到 **24 KB**。当前 bss **56,968 B（86.9% of 64 KB）**（2026-10-08 构建实测，口径与明细见 `docs/charter.md` §6），扣掉链接脚本的 1.5 KB 保留后余约 **8.4 KB**。硬故障现在会**闪两下 LED 再复位**（`Fault_reset_()`），和断言（`SM_onAssert`，打记录后复位）在灯上可分。
- flash 线程（`application/flash/`）：形态与 UI 线程同构——普通 FreeRTOS 任务 + 内嵌 HSM + 自建收件箱（深 8，环里只放**指针**），但**不挂任何周期时事件**，主循环是 `ulTaskNotifyTakeIndexed(0, portMAX_DELAY)`：这是它存在的理由，AO 的 8 格队列装不住 130 ms/扇区的擦除。静态 TCB + 256 words 栈、优先级 23（与 UI 同级；不能低于调用者——notify 等待没有优先级继承）。它是运行期 SPI2 的唯一使用者：调用者的请求块在自己的栈上，`Flash_probe()` / `Flash_readBlock()` 提交后阻塞在**自己的槽 1**上，flash 线程做完 `xTaskNotifyGiveIndexed` 唤醒——CPU 让给 idle，不是自旋；**通知槽约定**：槽 0 = 任务自己的入口唤醒（SST AO / UI 收件箱 / flash 线程收请求），槽 1 = flash RPC 完成，所以 `configTASK_NOTIFICATION_ARRAY_ENTRIES = 2`（每任务多约 8 B）；缓冲按**借用**语义交付（等待期间调用者不碰，完成后归还）。接口对所有调用者一视同仁，长操作的边界由生命周期管（升级只在启动早期/维护模式）。Blinky 第 60 拍的身份探测改走 `Flash_probe()`（记录仍是 25/26/32），每完成 1/8/16… 个请求报一次 `[MEM][STACK][FLASH]`（RecID 37）。**未上板验证**；`fwt` 自检任务仍自行驱动 SPI2，是待退场的例外。
- 上电画面：`BSP_lcdInit()` 的顺序是 **（仅首次）SWRESET → 等 130ms → SLPOUT → 等 130ms → 寄存器表 → 清 GRAM 成黑（`BSP_lcdFill_`，76,800 字，约 5.4 ms 总线）→ 回读四点验证 → DISPON → 背光**；**验证不过只重发「寄存器表 + 清屏」，不再重发 SWRESET/SLPOUT**，每 100 ms 一次、共 100 次（预算约 11 s）。两处顺序都是有原因的：**DISPON 必须在清屏之后**（面板一收到就扫描 GRAM，先清才不会有随机花屏，所以 0x29 从厂商表里挪出来了）；**背光必须最后**——`BSP_lcdInit()` 返回时面板已经是「整屏黑 + 背光点亮」的确定状态（所有重试都在暗屏下做，不可见），之后才轮到 UI 线程把 LVGL 画到这层黑底上。窗口设置 CASET/RASET/RAMWR 已提成公开 API（`BSP_lcdSetWindow` + `BSP_lcdWritePixels`），供 UI 线程的 LVGL flush 使用。
- **冷上电为什么慢：清屏要等面板"醒透"，而且这个时间不是定值。** 实测（同一天、多次冷上电）：**3018 / 3194 / 3816 ms**，还有一次**超过 4563 ms 仍没就绪**。变量源未定，最可能是**拔插速度**——拔了马上插，电源轨上的电容没放完电，下一次的上电起点就不同。**调试器在不在也影响它**：J-Link 接上时 3018~3194 ms，断开时 3816 ms 起（探针额外提供一条地线和 NRST 上的并联上拉）。所以预算必须按**最坏情况**留余量，不能贴任何单次测量。RESX 焊在板级 NRST 上（不是 GPIO），固件无法单独复位面板——这是面板初始化与 NRST 唯一的关联。**已排除的假设**：①"NRST 被电容拖慢、面板一直卡在复位里"——固件在上电后 ~0 ms 就开跑（第一次探测落在 394 ms），说明 NRST 早拉高了；②"CH340 在 ~3 s 给 NRST 一个脉冲"——NRST 共用，脉冲会把 MCU 一起复位，而实测固件连续跑了 3~4 秒没重启；③"FSMC 写时序临界"——放宽到 47% 余量后就绪时刻没变；④"排线信号完整性"——与③同源，一并放下。**当前假设（待验证）**：电源轨上升慢（MCU 在 VDD≈2.0V 就能跑，面板要 2.4V 以上且内部电路更稳），冷上电时 RESX 跟着电源爬、给的是**不合格的复位**（datasheet Note 4），而复位键/J-Link 给的是电源已稳之后的陡边沿。验证方法：量 3.3V 轨上升时间，或换非 USB 的供电方式试。
- **已知且已接受的失败模式：很冷时冷上电会花屏。** 面板就绪时刻随温度显著变长——暖机后 3.0~4.6 s，空调房十来度时 **>11 s**（约 20°C 温差带来 2.4~3.7 倍变化，对纯数字逻辑不正常，对面板内部的模拟/电源模块是可能的量级）。探测预算是 100 次（约 11.2 s），耗尽后固件**无条件** DISPON + 点背光，于是屏幕停在上电噪声；而此刻面板仍未就绪，LVGL 首帧同样写不进去、盖不住它，噪声就永久留下。**恢复方式：按一下复位键**（热复位约 0.4 s，因为电源已稳、面板已醒）。2026-10-02 决定暂不加大预算、也不把背光改成"仅成功才点"——因为加大只是把阈值推后，而黑屏并不比花屏更容易判断。要真正压掉它只能动硬件（把 RESX 从 NRST 断开引 GPIO，或查模组那颗 10µF 在低温下的表现）。**MCU 侧已排除**：`HSE_STARTUP_TIMEOUT` 是 100 ms（`bsp/platform/stm32f1xx_hal_conf.h:91`），超时会走 `Error_Handler()` 的 `while(1)` 死循环，而板子每次都能启动 → HSE 在 100 ms 内起振 → 所有等待都发生在 MCU 开跑之后，全在面板侧。
- **踩过的坑：不能用「重试」去砸面板的复位释放窗口。** 曾经是每 270 ms 发一次 SWRESET 的 4 次重试循环，实测冷上电必然花屏；改成「密集探测 + SWRESET 只在首次发」之后命中 3194 ms 并成功。手册 §7.4.5 Note 1 说明 NVM 校准（ID、VCOM 等）的加载挂在复位路径上、`tRT` 最长 120 ms，重复复位会不断重启这个加载过程。
- **`BSP_lcdFilled_` 只读 x=0 一列**（y = 0/159/160/319）。它能抓到整行宽的噪声（噪声是整行的），但**若漏写只发生在右半边，这个健检会放行**。要收紧就改成多列采样（左/中/右各一点）。
- SPI Flash（`bsp/devices/bsp_flash.c`）：板上器件丝印 **W25Q128**（16 MB），挂 SPI2：CS=PB12(`F_CS`)、SCK=PB13、MISO=PB14、MOSI=PB15（核心板 IO 表 73~76 行；SPI1 被 VS1053 占着）。`SPI2_MISO` 与无线插座 JP2 共网，板上既没上拉也没串阻（netlist 只有 MCU/JP2/U3 三处），MCU 侧因此留了内部上拉。工作时钟 **PCLK1/2 = 18 MHz**（`FLASH_SPI_PRESCALER`，就是外设上限）：四档扫频（2.25 / 4.5 / 9 / 18 MHz）每档同址连读 16 次，全部一致、四档字节也相同，所以没有理由降速；哪天要读写真数据时若怀疑时序，照这个办法再扫一遍即可。
- **这颗器件不是正牌 Winbond，但总线与器件本身已验证合格**：`9Fh` 稳定回 `52 21 18`、`90h` 回 `52 17`、`SR1=00`，地址 0 读到 FAT 引导扇区 `EB FE 90 4D 53 44`（OEM 名 `MSD...`，即出厂镜像）。`17`/`18` 正是 W25Q128 家族的器件 ID/容量字节，`EF`(Manuf)/`40`(Type) 却是 `52`/`21` —— 命令兼容但非正牌。**所以记录 26 在这块板上每次开机都会亮，那是预期结果，不是故障**（它的标签已从 `[??????]` 改成 `[NOT-W25Q128]`，免得看起来像个待查的哑谜）。另：mode 3 无应答，只用 mode 0。
- **ID 语义补充**：`9Fh` 首字节是 **JEP106 厂商码**（7 位数据 + 奇校验）。`52h` 校验自洽（数据位里 3 个 1 → 校验位 0），所以它不是 `EFh` 被读错一位的产物，器件确实这么答；具体厂商未确认（JEP106 表里 0x52 对应的是老厂 Alliance Semiconductor，对不上现代 flash 厂商）。另外，mode 3 那次的「无应答」是我们在代码里改 CPOL/CPHA 测的，可能怪我们自己，不要当成器件结论。
- **上电后第一个 SPI 事务不可靠**：连续三轮开机给出三个不同结果，之后每次都稳定（SPI 140 kHz、2.25 MHz 与 GPIO 位翻转到 25 kHz 三方一致）。`BSP_flashInit()` 里用一次丢弃的状态寄存器读把它吃掉。
- **读写自检两档**（`bsp_flash.c` 的 `FLASH_WT_MODE`）：`FLASH_WT_SAMPLE`（默认）每 64 个扇区抽一个、约十秒出结论；`FLASH_WT_FULL` 逐扇区走完整片、约十分钟（擦除约 130 ms/扇区，是本颗器件的实测值）。**两档都会擦掉它碰到的扇区**，所以等 flash 里存了真数据就设成 `FLASH_WT_OFF`。自检跑在自己的 `fwt` 任务里（优先级 20、栈 256 words），每扇区 `vTaskDelay(1)` 让 idle 继续泵 trace；结果记录是 34（start，含 SR1）/35（进度）/36（done：ok / eraseBad / progBad），任务退出前补一条 `[MEM][STACK][FLASH-WT]`（RecID 44）。**现在默认 `FLASH_WT_OFF`**：flash 已验收，而它每次开机都会擦写、且和 flash 线程抢 SPI2（两个写者一条总线），留着只会污染排查。关掉后 `fwt` 的 TCB+栈被 `--gc-sections` 丢掉，bss 从 58,096 降到 **56,968**。
- 排障脚手架（记录 27~31：140 kHz 对照、mode 3、90h、GPIO 位翻转）在验证完成后已退休，id 不回收——像 14~19 一样。`BSP_flashProbe()` 由 Blinky 在 `BLINKY_FLASH_BEAT`（第 60 拍 ≈ 上电 30 s）经 `Flash_probe()` 让 flash 线程调一次，只发 **25/26**（身份）+ **32**（地址 0 的 6 字节）。为此在 `stm32f1xx_hal_conf.h` 打开了 `HAL_SPI_MODULE_ENABLED` 并在 CMake 里加了 `stm32f1xx_hal_spi.c`。
- git：分支 `master`，`dc92582`，工作区有未提交改动（全静态分配、LVGL 接入、`bsp/` 目录分层、验证脚手架清理）。
- 屏（ST7789）IM 已短接成 8080-16bit（`R5/R7/R10` 短接）：`RDID=0x8552`、16 位 GRAM 回环、厂商初始化下的显示都实测通过。
- 板上 SRAM（IS62WV51216，FSMC Bank3/`NE3`）19 根地址线走线实测通过。**时序已从「功能探测档」收紧到器件安全上限**：原来是 ADDSET=1/DATAST=15（≈250 ns/次，≈8 MB/s，注释写明不是带宽测试）；现在读 `ADDSET=1/DATAST=1`（6 HCLK = 83.3 ns/次，tAA/tRC 余量 51%）= 24.0 MB/s，写 `ADDSET=1/DATAST=3`（7 HCLK = 97.2 ns/次，tPWE 余量 39%）= 20.6 MB/s。**踩过的坑：读侧 DATAST 不能给 0**——RM0008 Table 112（`FSMC_BTRx`）写着 *This value cannot be 0 (minimum is 1)*，给 0 之后 FSMC 在该 bank 第一次被访问时就把 CPU 卡住：整机冻死（灯不动、屏停在上一帧、连寄存器直写的调试标记都出不来），改回 1 立刻恢复。实测由 Blinky 第 60 拍的 `BSP_sramBench()`（4 KB 写 + 回读 + 校验，`[TIME][SRAM]`，RecID 42）给出，**待上板验证**；那一档理论值只对 -55 器件成立，丝印上的速度档还没确认。`ledOn/ledOff` 时间戳用 FreeRTOS tick：DWT `CYCCNT` 在这颗片子上脱离调试器会停摆（当初那条 `[CYCCNT]` 记录已随验证脚手架一起移除）。
- 硬件验证脚手架（GRAM16 回环、刷屏色带、`[CYCCNT]`、SRAM 地址线走线探针，RecID 14~19）已移除：LCD 侧只剩 `BSP_lcdInit()`（SLPOUT → 寄存器表 → 一次性 RDID），SRAM 侧只剩 `BSP_sramInit()`。`[MEM][STACK]`（21~23）保留，它是给 LVGL AO 栈和静态池定量用的量具。
- 启动顺序：`BSP_init()` → FreeRTOS 定时器 API → `Ui_start()` → `Flash_start()` → `AO_start()` → `SST_start()` → `vTaskStartScheduler()`。`BSP_init()`（`bsp.c`）内部按 tracer → 时钟 → LCD → SRAM 走：tracer 必须最先构造，否则面板 bring-up 的记录会被 `SM_Trace_push` 静默丢弃；LCD 那段含面板复位恢复 120 ms 与 SLPOUT 后 130 ms，阻塞约 250 ms。它必须在第一次 FreeRTOS 调用之前，因为定时器 API 会进临界区，调度器未启动时 `uxCriticalNesting` 是 `0xaaaaaaaa`，之后中断一直被关，`HAL_Delay` 会死等。面板 `RST` 挂在板级复位网络上，跟着 MCU 一起复位，所以那 120 ms 从复位起算。**「绑定先于派发」——通道的环与唤醒目标必须先于任何投递落地**，SST 核心自己就是这么做的（`SST_Task_start` 先落队列、再建任务、最后投初始事件，`sst.c:72-81`），应用层这两条通道原来不是：`AO_start()` 的初始转移（`Blinky_on` entry → `Ui_postLedOn()`，`blinky.c:106-111`）发生在 `Ui_start()` 之前，而 `Ui_wake_`/`Flash_wake_` 在未绑定时**静默跳过**（`ui_evt.c:49-55`、`flash_req.c:66-73`）——UI 那条只退化成"等一次 poll timeout"（≤10 ms，`ui_disp.c:36,124`），flash 那条则是调用方 park 在 `portMAX_DELAY`（`flash_req.c:113`）而永远等不到唤醒，此前只是**不可达**、不是不存在。换顺序不改可观测行为：调度器未启动时 `pxCurrentTCB` 取"优先级高者胜、同级后来者胜"（`tasks.c:2066-2077`），两种顺序最后都被 `vTaskStartScheduler` 里建的 Tmr 覆盖（`tasks.c:3762`）；调度器之前的 `xTaskNotifyGive` 合法且通知计数保留（出阻塞 + yield 的分支只在目标处于 `taskWAITING_NOTIFICATION` 时走，`tasks.c:8088,8114`），所以初始那次投递由"等超时"变成"投递即唤醒"。**未上板验证**（改动只有 `main.c` 的顺序与注释）。
- **CH340C 自动下载支路的坑（比「干扰 J-Link」严重得多）**：原理图连线是 `DTR# → R25 1K → Q1 SS8050(NPN) → D2 1N4148 → RESET`（netlist `PID201 = NLRESET`）与 `RTS# → R27 1K → Q2 SS8550(PNP) → R30 1K → BOOT0`（netlist `PIR3001 = NLBOOT0`）。多数终端**一打开串口就 assert DTR/RTS**，于是 RESET 被按低、BOOT0 被拉高：芯片要么一直不出复位，要么进 ROM bootloader——表现就是「串口上看不到上电执行情况」，与固件无关。两个解法：① **终端侧**：让 DTR/RTS 保持 deassert（pyserial 建口时 `dsrdtr=False, rtscts=False`，开完再 `ser.dtr = False; ser.rts = False`），然后按板上 RESET 键即可完整看到上电流程；② **硬件侧**：翘掉 `D2` 与 `R30` 断开这两条支路——`BOOT0` 有自己的 R12 10K 下拉（`PIR1201 = NLBOOT0`）、`RESET` 有自己的 R4 10K 上拉（`PIR401 = NLRESET`，同网还有复位键 SW1），正常启动不受影响，代价只是失去自动下载（我们用 J-Link 烧，不需要），顺带也解掉对 J-Link 的干扰。
- **已实测（2026-10-08）：解法①真的够用。** 串口**保持打开**时，只要 `ser.rts = False; ser.dtr = False`，CH340 那两条支路就**完全不干预 NRST** —— 板子的复位归 J-Link 独占，极性由此确定：**OFF = 不干扰**。所以「单脚本管全程」（先开串口 → J-Link 置标志+复位 → 握手）**不剪线也成立**。配合 pyserial 的实现细节（默认 `_dtr_state/_rts_state = True` 要 assert；setter 有 `is_open` 守卫）应当**先设 False 再 `open()`**，这样连打开那一瞬都不会 assert。解法②（翘 `D2`/`R30`）则是把这条从「约定」升级成「不可能发生」。**关闭串口同样没问题**（2026-10-08 实测）—— 开着、关着都不干预 NRST，所以**当前硬件不需要拆 `D2`/`R30`**，解法②只是可选加固（防“别的程序乱开串口”）。
- WSL 侧没有 J-Link 工具：烧录与 GDB 都由 Windows 侧程序承担，从 WSL 经 cmd.exe 调用；Keil/GDB 用 `.elf`，Commander 用 `.hex`（见上）。
- **镜像布局（2026-10-08 起，已上板验证）**：flash 前 16 KB 是 bootloader（`bootloader/`，自带 `stm32f1xx_bl.ld`），app 从 `0x08004000` 起（app 的 `.ld` + `main()` 第一句设 `SCB->VTOR`）——**CPU 复位永远从 `0x08000000` 取向量，所以只烧 app 不烧 BL 就起不来**，`tools/flash.jlink` 已改成两个 hex 都烧。两份 `.ld` 用同一公式留出 32 B 跨复位标志：`_noinit_base = ORIGIN(RAM)+LENGTH(RAM)-32`、`_estack` 同步下移、`_snoinit` = **`0x2000FFE0`**（RAM 顶；MSP 从它往下长，永不覆盖；跨复位保留、断电即失）。BL 现在只做：时钟 → UART1(收发 115200) → 读标志 → 200 ms 窗口 → 校验 app 向量表 → 交接，以及**烧写模式下的握手**；协议与 NOR 擦写还没写，text **6.7 KB**（16 KB 里）。**实机三态验过**：无标志 → 跳 app、`xTickCount` 在涨；置标志（J-Link `w4 0x2000FFE0, 0xB00710AD`）→ 停在 BL、app 冻结（BL 自己的 `uwTick` 在涨）；清标志 → 恢复正常。**踩过的坑**：喂 `HAL_IncTick` 的 `HAL_TIM_PeriodElapsedCallback` 实现在 `application/main.c` 里，BL 不链那个文件就落到 HAL 的空 weak 版本 → `uwTick` 永远 0 → BL 的 `HAL_Delay()` 卡死（现象：PC 停在 `HAL_GetTick`、TIM1 的 UIF 一直置位）；BL 现在自带一份。
- **烧写握手（2026-10-08，已上板联调）**：BL 在烧写模式下**只做握手、不碰 NOR**。线协议：请求 `AA | ver(1) | len(4 BE) | crc32(4 BE)`（10 B，CRC 覆盖 ver+len），回 `55 | base(4 BE) | block(2 BE) | crc32(4 BE)`（11 B，CRC 覆盖前 7 B），非法帧回 `4E`；CRC32 参数与 `zlib.crc32` 一致（poly `0xEDB88320`、init 全 1、末尾取反）——**整张资产的最终校验就是这个 CRC32**，块级载荷还没定。实机验过：好帧回 11 B（`55 00081000 1000 C62A1761`，Python 独立算的 CRC 一致）、坏帧回 `4E`；BL 侧计数（`Bl_frames_`/`Bl_ok_`/`Bl_bad_`/`Bl_len_` = `0x20000078`/`74`/`70`/`6C`）读到 2/1/1/**153600**。脚本侧 `tools/burn-asset.py --handshake-only` 用 pty 假目标机自测通过（请求帧合法、回帧 CRC 验过）。**串口在哪一侧**：CH340 = **COM12**、J-Link CDC = COM6（Windows 侧），WSL 里没有 CH340 → 脚本要在 Windows 上跑，需要 `py -m pip install pyserial`（WSL 侧 pyserial 已有 3.5，但没有口）；临时联调可用 PowerShell 的 `System.IO.Ports.SerialPort`（零安装，且能先设 `DtrEnable/RtsEnable = $false` 再 `Open()`）。
- `SM_onAssert` 里的 UART 发送是无限等待，这是知情保留的现状。
- HAL 层错误出口（`BSP_onFatal` / `BSP_onFault` 收敛到统一复位终点）和断言持久化（`.noinit` SRAM 转存 Flash）都还没实现。
