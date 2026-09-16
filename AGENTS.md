# AGENTS.md

STM32F103ZET6 固件骨架：FreeRTOS 内核 + `sm_sst` 的 AO/事件层 + HMI 外设口。
结构参考 `sm_tracer_tui`，板级参数参考已点亮的 Cube 工程 `free_rtos_blinky`。
不用 Keil，不用 STM32CubeMX。

## 构建：走 Windows 工具链

从 WSL 驱动 Windows 侧工具链。把 shell 的工作目录放在 `/mnt/c` 下，避免 `cmd.exe` 报 UNC 路径告警。

```sh
/mnt/c/Windows/System32/cmd.exe /c "cd /d C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky && cmake --preset debug && cmake --build --preset build-debug"
```

- Windows 侧依赖：`cmake`/`ninja` 在 `C:\msys64\ucrt64\bin`，`arm-none-eabi-gcc` 15.2.1 在 `C:\mengrendufu\software_ins\mingw\arm_gnu\bin`。
- 产物：`build/debug/sm_blinky.elf`、`.hex`、`.map`。
- 已知告警（无害）：`cmd.exe` 会打印 UNC 与路径相关的中文提示，可忽略。
- 不要跨平台复用同一个构建目录：`build/` 里是 Windows 生成的 `CMakeCache.txt` 和 `compile_commands.json`（`.clangd` 指向它）。需要在 WSL 侧单独验证时，另开一个 binary dir。

## 测试：宿主机契约测试

`tests/` 下的测试**尚未接入 CMake/CTest**，需手工调用。都从仓库根目录执行。

```sh
# 事件池 GC 契约（poolId / refCtr / 静态事件不回收）
/mnt/c/Windows/System32/cmd.exe /c "cd /d C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky && gcc -std=c11 -Itests/ports -I3rd_party/sm_sst/include -Iports/static_pool -I3rd_party/common_c/include tests/test_sst_evt_pool.c ports/static_pool/sst_evt_pool.c 3rd_party/common_c/src/static_pool/static_pool.c -o %TEMP%\t_evt_pool.exe && %TEMP%\t_evt_pool.exe"

# configASSERT 转发到 SM_onAssert（模块名 / 行号）
/mnt/c/Windows/System32/cmd.exe /c "cd /d C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky && gcc -std=c11 -Ibsp/free_rtos -I3rd_party/sm_hsm/include tests/test_freertos_assert.c -o %TEMP%\t_assert.exe && %TEMP%\t_assert.exe"

# 复合优先级 0xRRSS 布局的编译期契约（低字节 SST，高字节 FreeRTOS）
/mnt/c/Windows/System32/cmd.exe /c "cd /d C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky && arm-none-eabi-gcc -std=c11 -DSTM32F103xE -DUSE_HAL_DRIVER -Iports -Iports/static_pool -Ibsp/free_rtos -I3rd_party/sm_sst/include -I3rd_party/sm_hsm/include -I3rd_party/common_c/include -I3rd_party/FreeRTOS-Kernel/include -I3rd_party/FreeRTOS-Kernel/portable/GCC/ARM_CM3 -I3rd_party/stm32f1xx-hal-driver/Inc -I3rd_party/cmsis-core/CMSIS/Core/Include -I3rd_party/cmsis-device-f1/Include -c tests/test_sst_priority.c -o %TEMP%\t_prio.o"
```

注意：

- 事件池测试**不要**链 `3rd_party/sm_sst/sst_c/src/sst.c`。`sst.c` 里未用到的 `SST_Task_run`/`SST_Task_post` 会拖入 `tests/ports/sst_port.h` 没有实现的 port 符号（`SST_start`、`SST_onIdle`、`SST_Task_setPrio`、`SST_PORT_TASK_PEND`）而链接失败。
- 宿主机 gcc 16（MSYS2 UCRT64）把 implicit-function-declaration 当错误，gcc 13 只是 warning。真要链 `sst.c` 时加 `-Wno-error=implicit-function-declaration`，并补 port 桩。MinGW 下 `-Wl,--gc-sections` 不会丢掉这些未引用函数。
- 测试可执行文件写到 `%TEMP%`，不要落进仓库。

## 烧录：J-Link Commander

`tools\flash.cmd` 一条命令搞定：编译 debug preset → SWD 烧录 → 复位运行。在 Windows cmd 里从**任意目录**都能调，路径按脚本自身位置解析。

```bat
C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky\tools\flash.cmd
C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky\tools\flash.cmd -nobuild
```

从 WSL 调用一样：`/mnt/c/Windows/System32/cmd.exe /c "C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky\tools\flash.cmd"`。

它内部就是下面这两条，要手调参数时直接用：

```sh
cmake --preset debug && cmake --build --preset build-debug
C:\mengrendufu\software_ins\JLink\JLink.exe -device STM32F103ZE -if SWD -speed 4000 -autoconnect 1 -ExitOnError 1 -NoGui 1 -CommandFile C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky\tools\flash.jlink
```

- Commander 在 `C:\mengrendufu\software_ins\JLink\JLink.exe`（V6.86）。加不加 PATH 都能跑：加了能简写成 `JLink.exe`，不加就写全路径（脚本里就是全路径）。
- `tools/flash.jlink`：**只放连上目标之后的动作**（`r`/`h`/`loadfile`/`r`/`g`/`qc`），连接参数留在命令行，避免同一个事实写两处。一行一条命令，支持 `//` 注释；`loadfile` 自带擦除与校验，不用先 `erase`。
- 本版（V6.86）`loadfile` 的支持格式只有 `bin/mot/hex/srec`，所以烧 `build\debug\sm_blinky.hex`；`.elf` 留给 GDB/Keil。
- 失败判断：脚本以非 0 退出并打印 `[flash] FAILED (ERRORLEVEL=...)`；`ERRORLEVEL` 非 0 来自 `-ExitOnError 1`。
- 会话里敲 `?` 就是完整命令表。日常够用的：`st`、`Regs`、`mem32 68000000, 8`、`SetBP`/`ClrBP`、`g`、`r`、`qc`。
- 别用管道或重定向喂 `JLink.exe`（`JLink.exe < /dev/null` 之类）：它会在 `J-Link>` 空转，并且一直占着探针句柄，Keil 那边会连不上。跑完也可能残留 `JLink.exe`/`JLinkGUIServer.exe`：`tasklist | findstr /I jlink` 查一下，占了就 `taskkill /F /PID <pid>`。

## 目录与依赖

| 目录 | 内容 |
|---|---|
| `application/` | `main.c`、`application.c`（AO 启动）、`aos/blinky/`（唯一 AO/HSM） |
| `bsp/` | 根下只有 `bsp.[ch]`：板级基础（时钟/DWT、LED 引脚与开关、`SM_onAssert`、tracer 出口与构造、`BSP_memReport`） |
| `bsp/devices/` | FSMC 上的两颗器件：`bsp_lcd`（ST7789，总线 + 面板 bring-up）、`bsp_sram`（IS62WV51216，总线初始化） |
| `bsp/free_rtos/` | FreeRTOS：`FreeRTOSConfig.h` 与 `freertos_hooks.c`（idle/timer 静态内存、栈溢出钩子） |
| `bsp/lvgl/` | LVGL 配置（`lv_conf.h`）；将来显示适配层也放这里 |
| `bsp/platform/` | 平台样板：HAL conf、MSP、TIM1 时基、中断向量、newlib 桩（`syscalls`/`sysmem`）、链接脚本。原来的 `main.h` 已删除，LED 引脚宏移到 `bsp.h` |
| `ports/` | SST 骑 FreeRTOS 的口；`ports/static_pool/` 是事件池适配 |
| `tests/` | 宿主机契约测试，未接 CMake |
| `tools/` | Windows 侧烧录脚本（`flash.cmd`/`flash.jlink`）与 `check_line_width.py` |
| `3rd_party/` | 7 个 submodule，全部以云端仓库为准 |

依赖：FreeRTOS-Kernel、common_c、sm_hsm、sm_sst、CMSIS Core 5.4.0、CMSIS Device F1 v4.3.3、STM32F1 HAL v1.1.8。
第三方统一放 `3rd_party/`，不要新建 `vendor/` 或 `third_party/`。

## 改动约定

- **列宽 78，管的是行不是文件**：我们自己新增/修改的行不超过 78 **显示列**（CJK 字符按两列算，不是字节数）。模板与第三方原文——`bsp/platform/` 的 ST 80 列横线和许可文本、`bsp/free_rtos/FreeRTOSConfig.h` 的 Amazon 模板文字——保持原样；即使我们就在这些文件里改代码，也只约束我们加的那几行，原有超长行一律不动。检查：`python3 tools/check_line_width.py` 默认只量工作区相对 HEAD 新增/改写的行，外加未跟踪新文件整份；点名文件时整份量；非 0 退出即超标。`.editorconfig` 登记同一条规则，只有 `3rd_party/` 镜像与 `tools/flash.jlink` 的不可断行绝对路径豁免。
- **行尾不做统一**：树里 CRLF 与 LF 是混的（历史编辑 + 补丁插行造成的），这是知情保留的状态——不要为了"整齐"把某个文件整份转行尾，那会产生一整个文件级的 diff，把真实改动淹掉。`.editorconfig` 因此也没有钉 `end_of_line`。
- **固件编译与烧录只在用户明确要求时执行**（说"编译"或"烧录"）。平时只做只读检查、读代码、跑宿主机契约测试；真要编/烧时，先说明跑哪条命令、烧哪份产物（时间戳 + 大小）。
- **FreeRTOS 走全静态分配**（`configSUPPORT_DYNAMIC_ALLOCATION 0`，不编 `heap_4.c`）。任务栈由各 AO 自己声明（`StaticTask_t` + `StackType_t[]`，见 `blinky.c`），idle/timer 任务的内存由 `bsp/free_rtos/freertos_hooks.c` 提供；新增 AO 必须自带静态栈。栈够不够用 `[MEM][STACK]` 记录（`BSP_memReport`）的高水位来定，不靠猜。
- 依赖以云端 submodule 为准；本地拷贝只作对照。适配远端时允许改 `ports/`、CMake、BSP，但保留 `free_rtos_blinky` 已验证的板级参数。
- 要改 `sm_sst` 时：先在本地镜像 `C:\mengrendufu\workshop\local_prjs\sm_sst` 改并推送，再回本仓库同步 submodule 指针。
- 优先级编码 `0xRRSS`：低字节是 SST 逻辑优先级（用 `SST_MAX_TASK` 校验），高字节是 FreeRTOS 任务优先级（用 `configMAX_PRIORITIES` 校验）。契约检查留在 SST 核心公共入口，port 只额外校验高字节。
- 断言输出允许丢日志，但不允许卡死；复位优先于打印。

## 当前状态与已知坑

- git：分支 `master`，`dc92582`，工作区有未提交改动（全静态分配、LVGL 接入、`bsp/` 目录分层、验证脚手架清理）。
- 屏（ST7789）IM 已短接成 8080-16bit（`R5/R7/R10` 短接）：`RDID=0x8552`、16 位 GRAM 回环、厂商初始化下的显示都实测通过。
- 板上 SRAM（IS62WV51216，FSMC Bank3/`NE3`）19 根地址线走线实测通过。`ledOn/ledOff` 时间戳用 FreeRTOS tick：DWT `CYCCNT` 在这颗片子上脱离调试器会停摆（当初那条 `[CYCCNT]` 记录已随验证脚手架一起移除）。
- 硬件验证脚手架（GRAM16 回环、刷屏色带、`[CYCCNT]`、SRAM 地址线走线探针，RecID 14~19）已移除：LCD 侧只剩 `BSP_lcdInit()`（SLPOUT → 寄存器表 → 一次性 RDID），SRAM 侧只剩 `BSP_sramInit()`。`[MEM][STACK]`（21~23）保留，它是给 LVGL AO 栈和静态池定量用的量具。
- 启动顺序：`BSP_init()` → FreeRTOS 定时器 API → `AO_start()` → `vTaskStartScheduler()`。`BSP_init()`（`bsp.c`）内部按 tracer → 时钟 → LCD → SRAM 走：tracer 必须最先构造，否则面板 bring-up 的记录会被 `SM_Trace_push` 静默丢弃；LCD 那段含面板复位恢复 120 ms 与 SLPOUT 后 130 ms，阻塞约 250 ms。它必须在第一次 FreeRTOS 调用之前，因为定时器 API 会进临界区，调度器未启动时 `uxCriticalNesting` 是 `0xaaaaaaaa`，之后中断一直被关，`HAL_Delay` 会死等。面板 `RST` 挂在板级复位网络上，跟着 MCU 一起复位，所以那 120 ms 从复位起算。
- 板上 CH340 的 DTR/RTS 自动下载支路会干扰 J-Link；`BOOT0`(R12 10K 下拉)、`BOOT1/PB2`(R13 10K 下拉)、`RESET`(R4 10K 上拉) 都有独立上下拉，可安全翘起 `D2` 与 `R30`。
- WSL 侧没有 J-Link 工具，烧录与 GDB 只能在 Windows 侧做：Keil/GDB 用 `.elf`，Commander 用 `.hex`（见上）。
- `SM_onAssert` 里的 UART 发送是无限等待，这是知情保留的现状。
- HAL 层错误出口（`BSP_onFatal` / `BSP_onFault` 收敛到统一复位终点）和断言持久化（`.noinit` SRAM 转存 Flash）都还没实现。
