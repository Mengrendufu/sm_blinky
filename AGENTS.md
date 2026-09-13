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
/mnt/c/Windows/System32/cmd.exe /c "cd /d C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky && gcc -std=c11 -Ibsp -I3rd_party/sm_hsm/include tests/test_freertos_assert.c -o %TEMP%\t_assert.exe && %TEMP%\t_assert.exe"

# 复合优先级 0xRRSS 布局的编译期契约（低字节 SST，高字节 FreeRTOS）
/mnt/c/Windows/System32/cmd.exe /c "cd /d C:\mengrendufu\workshop\local_prjs\embeded\hmi\sm_blinky && arm-none-eabi-gcc -std=c11 -DSTM32F103xE -DUSE_HAL_DRIVER -Iports -Iports/static_pool -Ibsp -I3rd_party/sm_sst/include -I3rd_party/sm_hsm/include -I3rd_party/common_c/include -I3rd_party/FreeRTOS-Kernel/include -I3rd_party/FreeRTOS-Kernel/portable/GCC/ARM_CM3 -I3rd_party/stm32f1xx-hal-driver/Inc -I3rd_party/cmsis-core/CMSIS/Core/Include -I3rd_party/cmsis-device-f1/Include -c tests/test_sst_priority.c -o %TEMP%\t_prio.o"
```

注意：

- 事件池测试**不要**链 `3rd_party/sm_sst/sst_c/src/sst.c`。`sst.c` 里未用到的 `SST_Task_run`/`SST_Task_post` 会拖入 `tests/ports/sst_port.h` 没有实现的 port 符号（`SST_start`、`SST_onIdle`、`SST_Task_setPrio`、`SST_PORT_TASK_PEND`）而链接失败。
- 宿主机 gcc 16（MSYS2 UCRT64）把 implicit-function-declaration 当错误，gcc 13 只是 warning。真要链 `sst.c` 时加 `-Wno-error=implicit-function-declaration`，并补 port 桩。MinGW 下 `-Wl,--gc-sections` 不会丢掉这些未引用函数。
- 测试可执行文件写到 `%TEMP%`，不要落进仓库。

## 目录与依赖

| 目录 | 内容 |
|---|---|
| `application/` | `main.c`、`application.c`（AO 启动）、`aos/blinky/`（唯一 AO/HSM） |
| `bsp/` | 时钟、`FreeRTOSConfig.h`、`STM32F103ZETX_FLASH.ld`、HAL conf、中断、`SM_onAssert` |
| `ports/` | SST 骑 FreeRTOS 的口；`ports/static_pool/` 是事件池适配 |
| `tests/` | 宿主机契约测试，未接 CMake |
| `3rd_party/` | 7 个 submodule，全部以云端仓库为准 |

依赖：FreeRTOS-Kernel、common_c、sm_hsm、sm_sst、CMSIS Core 5.4.0、CMSIS Device F1 v4.3.3、STM32F1 HAL v1.1.8。
第三方统一放 `3rd_party/`，不要新建 `vendor/` 或 `third_party/`。

## 改动约定

- 依赖以云端 submodule 为准；本地拷贝只作对照。适配远端时允许改 `ports/`、CMake、BSP，但保留 `free_rtos_blinky` 已验证的板级参数。
- 要改 `sm_sst` 时：先在本地镜像 `C:\mengrendufu\workshop\local_prjs\sm_sst` 改并推送，再回本仓库同步 submodule 指针。
- 优先级编码 `0xRRSS`：低字节是 SST 逻辑优先级（用 `SST_MAX_TASK` 校验），高字节是 FreeRTOS 任务优先级（用 `configMAX_PRIORITIES` 校验）。契约检查留在 SST 核心公共入口，port 只额外校验高字节。
- 断言输出允许丢日志，但不允许卡死；复位优先于打印。

## 当前状态与已知坑

- git：分支 `master`，`5eb83aa`，工作区干净。
- 屏（ST7789）的 IM 配置电阻尚未焊成 80-16bit（目标焊 `R5/R7/R10`，不焊 `R6/R8/R9`）。**在此之前不要碰 FSMC/ST7789**，也不上 LVGL。
- 板上 CH340 的 DTR/RTS 自动下载支路会干扰 J-Link；`BOOT0`(R12 10K 下拉)、`BOOT1/PB2`(R13 10K 下拉)、`RESET`(R4 10K 上拉) 都有独立上下拉，可安全翘起 `D2` 与 `R30`。
- WSL 侧没有 J-Link 工具，烧录与 GDB 只能在 Windows 侧做。只烧 `.elf`（GDB 要符号）。
- `SM_onAssert` 里的 UART 发送是无限等待，这是知情保留的现状。
- HAL 层错误出口（`BSP_onFatal` / `BSP_onFault` 收敛到统一复位终点）和断言持久化（`.noinit` SRAM 转存 Flash）都还没实现。
