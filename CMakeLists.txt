cmake_minimum_required(VERSION 3.25)
project(sm_blinky LANGUAGES C ASM)

set(ROOT_SST      ${CMAKE_SOURCE_DIR}/3rd_party/sm_sst)
set(ROOT_HSM      ${CMAKE_SOURCE_DIR}/3rd_party/sm_hsm)
set(ROOT_COMMON   ${CMAKE_SOURCE_DIR}/3rd_party/common_c)
set(ROOT_CMSIS_CORE   ${CMAKE_SOURCE_DIR}/3rd_party/cmsis-core)
set(ROOT_CMSIS_DEVICE ${CMAKE_SOURCE_DIR}/3rd_party/cmsis-device-f1)
set(ROOT_HAL      ${CMAKE_SOURCE_DIR}/3rd_party/stm32f1xx-hal-driver)
set(ROOT_FREERTOS ${CMAKE_SOURCE_DIR}/3rd_party/FreeRTOS-Kernel)

set(SRC_FREERTOS
    ${ROOT_FREERTOS}/tasks.c
    ${ROOT_FREERTOS}/queue.c
    ${ROOT_FREERTOS}/list.c
    ${ROOT_FREERTOS}/timers.c
    ${ROOT_FREERTOS}/portable/GCC/ARM_CM3/port.c
)

set(SRC_HAL
    ${ROOT_HAL}/Src/stm32f1xx_hal.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_cortex.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_dma.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_exti.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_flash.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_flash_ex.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_gpio.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_gpio_ex.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_pwr.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_rcc.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_rcc_ex.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_tim.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_tim_ex.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_uart.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_spi.c
    ${ROOT_HAL}/Src/stm32f1xx_hal_sram.c
    ${ROOT_HAL}/Src/stm32f1xx_ll_fsmc.c
)

set(SRC_ALL
    ${CMAKE_SOURCE_DIR}/application/main.c
    ${CMAKE_SOURCE_DIR}/application/application.c
    ${CMAKE_SOURCE_DIR}/application/asset.c
    ${CMAKE_SOURCE_DIR}/application/aos/blinky/blinky.c
    ${CMAKE_SOURCE_DIR}/application/ui/thread/ui.c
    ${CMAKE_SOURCE_DIR}/application/ui/thread/ui_evt.c
    ${CMAKE_SOURCE_DIR}/application/ui/thread/ui_disp.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/widget_io_priv.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/w_topbar.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/w_dots.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/w_gauge.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/w_barpanel.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/w_switch.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/w_dial.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/w_nvm.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/widgets/w_ram.c
    ${CMAKE_SOURCE_DIR}/application/ui/hsm/sm_ui.c
    ${CMAKE_SOURCE_DIR}/application/nvm/nvm.c
    ${CMAKE_SOURCE_DIR}/application/aos/flash
    ${CMAKE_SOURCE_DIR}/3rd_party/sm_nvs/include
    ${CMAKE_SOURCE_DIR}/application/aos/flash/flash.c
    ${CMAKE_SOURCE_DIR}/ports/nvs_port.c
    ${CMAKE_SOURCE_DIR}/3rd_party/sm_nvs/src/nvs.c
    ${CMAKE_SOURCE_DIR}/3rd_party/sm_nvs/src/nvs_crc.c
    ${CMAKE_SOURCE_DIR}/application/aos/flash/flash_req.c
    ${CMAKE_SOURCE_DIR}/application/aos/flash/hsm/sm_flash.c

    ${CMAKE_SOURCE_DIR}/bsp/bsp.c
    ${CMAKE_SOURCE_DIR}/bsp/devices/bsp_lcd.c
    ${CMAKE_SOURCE_DIR}/bsp/devices/bsp_sram.c
    ${CMAKE_SOURCE_DIR}/bsp/devices/bsp_flash.c
    ${CMAKE_SOURCE_DIR}/bsp/free_rtos/freertos_hooks.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/stm32f1xx_it.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/stm32f1xx_hal_msp.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/stm32f1xx_hal_timebase_tim.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/syscalls.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/sysmem.c

    ${ROOT_CMSIS_DEVICE}/Source/Templates/system_stm32f1xx.c
    ${ROOT_CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32f103xe.s
    ${CMAKE_SOURCE_DIR}/ports/sst_port.c
    ${CMAKE_SOURCE_DIR}/ports/static_pool/sst_evt_pool.c
    ${ROOT_SST}/sst_c/src/sst.c
    ${ROOT_HSM}/src/sm_hsm.c
    ${ROOT_HSM}/src/sm_tracer.c
    ${ROOT_COMMON}/src/static_pool/static_pool.c
    ${ROOT_COMMON}/src/crc/crc32.c
    ${SRC_FREERTOS}
    ${SRC_HAL}
)


# The vendored NVS keeps upstream's spelling; these two suppressions cover
# exactly the warnings that come from that and nothing else.
set_source_files_properties(${CMAKE_SOURCE_DIR}/3rd_party/sm_nvs/src/nvs.c
    PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-parameter")

add_executable(${PROJECT_NAME}.elf ${SRC_ALL})

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/3rd_party/sm_nvs/include
    ${CMAKE_SOURCE_DIR}/3rd_party/sm_nvs/src
    ${CMAKE_SOURCE_DIR}/3rd_party/sm_nvs/include
    ${CMAKE_SOURCE_DIR}/3rd_party/sm_nvs/src
    ${CMAKE_SOURCE_DIR}/application
    ${CMAKE_SOURCE_DIR}/application/ui
    ${CMAKE_SOURCE_DIR}/application/nvm
    ${CMAKE_SOURCE_DIR}/application/ui/thread
    ${CMAKE_SOURCE_DIR}/application/ui/hsm
    ${CMAKE_SOURCE_DIR}/application/aos/flash
    ${CMAKE_SOURCE_DIR}/application/aos/flash/hsm
    ${CMAKE_SOURCE_DIR}/bsp
    ${CMAKE_SOURCE_DIR}/bsp/devices
    ${CMAKE_SOURCE_DIR}/bsp/free_rtos
    ${CMAKE_SOURCE_DIR}/bsp/platform
    ${CMAKE_SOURCE_DIR}/ports
    ${CMAKE_SOURCE_DIR}/ports/static_pool
    ${CMAKE_SOURCE_DIR}/ports/common_c
    ${ROOT_SST}/include
    ${ROOT_HSM}/include
    ${ROOT_CMSIS_CORE}/CMSIS/Core/Include
    ${ROOT_CMSIS_DEVICE}/Include
    ${ROOT_HAL}/Inc
    ${ROOT_HAL}/Inc/Legacy
    ${ROOT_FREERTOS}/include
    ${ROOT_FREERTOS}/portable/GCC/ARM_CM3
    ${ROOT_COMMON}/include
)

#=============================================================================
# ===LVGL v9 (3rd_party/lvgl), built as its own library.
# The library only carries LVGL's sources, include paths and its own warning
# level; the configuration is ours (bsp/lvgl/lv_conf.h), so a feature that is
# switched off there compiles out inside LVGL itself and the glob stays put.
file(GLOB_RECURSE LVGL_SRC CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/3rd_party/lvgl/src/*.c
)

add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/3rd_party/lvgl
    ${CMAKE_SOURCE_DIR}/3rd_party/lvgl/src
)
target_include_directories(lvgl PRIVATE
    ${ROOT_HSM}/include      # sm_assert.h, used by LV_ASSERT_HANDLER
    ${ROOT_COMMON}/include
)
target_compile_definitions(lvgl PUBLIC
    LV_CONF_PATH="${CMAKE_SOURCE_DIR}/bsp/lvgl/lv_conf.h"
)
# Third party code: do not drown the firmware build in its -Wextra output.
target_compile_options(lvgl PRIVATE -w)

target_link_libraries(${PROJECT_NAME}.elf PRIVATE lvgl)

target_link_options(${PROJECT_NAME}.elf PRIVATE
    -T${CMAKE_SOURCE_DIR}/bsp/platform/STM32F103ZETX_FLASH.ld
    -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf>
            ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
)

#=============================================================================
# ===Bootloader: the first 16 KB of the flash (bootloader/stm32f1xx_bl.ld).
# Bare metal by design - no RTOS, no LVGL, no sm_sst: nothing runs before it,
# so the only things it needs are HAL, CMSIS, the startup code and the platform
# files the app also uses (clock time base, MSP, syscalls).
set(SRC_BL
    ${CMAKE_SOURCE_DIR}/bootloader/bl_main.c
    ${CMAKE_SOURCE_DIR}/bsp/devices/bsp_flash.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/stm32f1xx_it.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/stm32f1xx_hal_msp.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/stm32f1xx_hal_timebase_tim.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/syscalls.c
    ${CMAKE_SOURCE_DIR}/bsp/platform/sysmem.c
    ${ROOT_CMSIS_DEVICE}/Source/Templates/system_stm32f1xx.c
    ${ROOT_CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32f103xe.s
    ${SRC_HAL}
)

add_executable(bootloader.elf ${SRC_BL})

target_include_directories(bootloader.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/bootloader
    ${CMAKE_SOURCE_DIR}/bsp
    ${CMAKE_SOURCE_DIR}/bsp/devices
    ${CMAKE_SOURCE_DIR}/bsp/free_rtos
    ${CMAKE_SOURCE_DIR}/bsp/platform
    ${CMAKE_SOURCE_DIR}/ports
    ${ROOT_HSM}/include
    ${ROOT_COMMON}/include
    ${ROOT_FREERTOS}/include
    ${ROOT_FREERTOS}/portable/GCC/ARM_CM3
    ${ROOT_CMSIS_CORE}/CMSIS/Core/Include
    ${ROOT_CMSIS_DEVICE}/Include
    ${ROOT_HAL}/Inc
    ${ROOT_HAL}/Inc/Legacy
)

target_link_options(bootloader.elf PRIVATE
    -T${CMAKE_SOURCE_DIR}/bootloader/stm32f1xx_bl.ld
    -Wl,-Map=${CMAKE_BINARY_DIR}/bootloader.map
)

add_custom_command(TARGET bootloader.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:bootloader.elf>
            ${CMAKE_BINARY_DIR}/bootloader.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:bootloader.elf>
)
