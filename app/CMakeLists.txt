cmake_minimum_required(VERSION 3.25)
project(sm_blinky LANGUAGES C ASM)
set(APP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(REPO_ROOT "${APP_ROOT}/.." ABSOLUTE)
if(NOT TARGET project_common)
    add_subdirectory(${REPO_ROOT}/project_common
                     ${CMAKE_BINARY_DIR}/project_common)
endif()

set(ROOT_SST      ${REPO_ROOT}/3rd_party/sm_sst)
set(ROOT_HSM      ${REPO_ROOT}/3rd_party/sm_hsm)
set(ROOT_COMMON   ${REPO_ROOT}/3rd_party/common_c)
set(ROOT_CMSIS_CORE   ${REPO_ROOT}/3rd_party/cmsis-core)
set(ROOT_CMSIS_DEVICE ${REPO_ROOT}/3rd_party/cmsis-device-f1)
set(ROOT_HAL      ${REPO_ROOT}/3rd_party/stm32f1xx-hal-driver)
set(ROOT_FREERTOS ${REPO_ROOT}/3rd_party/FreeRTOS-Kernel)

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
    ${APP_ROOT}/application/main.c
    ${APP_ROOT}/application/application.c
    ${APP_ROOT}/application/asset.c
    ${APP_ROOT}/application/aos/blinky/blinky.c
    ${APP_ROOT}/application/ui/thread/ui.c
    ${APP_ROOT}/application/ui/thread/ui_evt.c
    ${APP_ROOT}/application/ui/thread/ui_disp.c
    ${APP_ROOT}/application/ui/hsm/widgets/widget_io_priv.c
    ${APP_ROOT}/application/ui/hsm/widgets/w_topbar.c
    ${APP_ROOT}/application/ui/hsm/widgets/w_dots.c
    ${APP_ROOT}/application/ui/hsm/widgets/w_gauge.c
    ${APP_ROOT}/application/ui/hsm/widgets/w_barpanel.c
    ${APP_ROOT}/application/ui/hsm/widgets/w_switch.c
    ${APP_ROOT}/application/ui/hsm/widgets/w_dial.c
    ${APP_ROOT}/application/ui/hsm/widgets/w_nvm.c
    ${APP_ROOT}/application/ui/hsm/widgets/w_ram.c
    ${APP_ROOT}/application/ui/hsm/widgets/ui_surface.c
    ${APP_ROOT}/application/ui/hsm/metrics/ui_metrics.c
    ${APP_ROOT}/application/ui/hsm/sm_ui.c
    ${APP_ROOT}/application/nvm/nvm.c
    ${APP_ROOT}/application/aos/flash
    ${REPO_ROOT}/3rd_party/sm_nvs/include
    ${APP_ROOT}/application/aos/flash/flash.c
    ${APP_ROOT}/ports/nvs_port.c
    ${REPO_ROOT}/3rd_party/sm_nvs/src/nvs.c
    ${REPO_ROOT}/3rd_party/sm_nvs/src/nvs_crc.c
    ${APP_ROOT}/application/aos/flash/flash_req.c
    ${APP_ROOT}/application/aos/flash/hsm/sm_flash.c

    ${APP_ROOT}/bsp/bsp.c
    ${APP_ROOT}/bsp/devices/bsp_lcd.c
    ${APP_ROOT}/bsp/devices/bsp_sram.c
    ${APP_ROOT}/bsp/free_rtos/freertos_hooks.c
    ${APP_ROOT}/bsp/free_rtos/bsp_tick.c
    ${APP_ROOT}/bsp/platform/stm32f1xx_it.c
    ${APP_ROOT}/bsp/platform/stm32f1xx_hal_msp.c
    ${APP_ROOT}/bsp/platform/stm32f1xx_hal_timebase_tim.c
    ${APP_ROOT}/bsp/platform/syscalls.c
    ${APP_ROOT}/bsp/platform/sysmem.c

    ${ROOT_CMSIS_DEVICE}/Source/Templates/system_stm32f1xx.c
    ${ROOT_CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32f103xe.s
    ${APP_ROOT}/ports/sst_port.c
    ${ROOT_COMMON}/src/time_evt/time_evt.c
    ${APP_ROOT}/ports/static_pool/sst_evt_pool.c
    ${ROOT_SST}/sst_c/src/sst.c
    ${ROOT_SST}/sst_c/src/sst_pubsub.c
    ${ROOT_HSM}/src/sm_hsm.c
    ${ROOT_HSM}/src/sm_tracer.c
    ${ROOT_COMMON}/src/static_pool/static_pool.c
    ${ROOT_COMMON}/src/crc/crc32.c
    ${SRC_FREERTOS}
    ${SRC_HAL}
)


# The vendored NVS keeps upstream's spelling; these two suppressions cover
# exactly the warnings that come from that and nothing else.
set_source_files_properties(${REPO_ROOT}/3rd_party/sm_nvs/src/nvs.c
    PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-parameter")

add_executable(${PROJECT_NAME}.elf ${SRC_ALL})

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${REPO_ROOT}/3rd_party/sm_nvs/include
    ${REPO_ROOT}/3rd_party/sm_nvs/src
    ${REPO_ROOT}/3rd_party/sm_nvs/include
    ${REPO_ROOT}/3rd_party/sm_nvs/src
    ${APP_ROOT}/application
    ${APP_ROOT}/application/ui
    ${APP_ROOT}/application/nvm
    ${APP_ROOT}/application/ui/thread
    ${APP_ROOT}/application/ui/hsm
    ${APP_ROOT}/application/aos/flash
    ${APP_ROOT}/application/aos/flash/hsm
    ${APP_ROOT}/bsp
    ${APP_ROOT}/bsp/devices
    ${APP_ROOT}/bsp/free_rtos
    ${APP_ROOT}/bsp/platform
    ${APP_ROOT}/ports
    ${APP_ROOT}/ports/static_pool
    ${APP_ROOT}/ports/common_c
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
    ${REPO_ROOT}/3rd_party/lvgl/src/*.c
)

add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl SYSTEM PUBLIC
    ${REPO_ROOT}/3rd_party/lvgl
    ${REPO_ROOT}/3rd_party/lvgl/src
)
target_include_directories(lvgl PRIVATE
    ${ROOT_HSM}/include      # sm_assert.h, used by LV_ASSERT_HANDLER
    ${ROOT_COMMON}/include
)
target_compile_definitions(lvgl PUBLIC
    LV_CONF_PATH="${APP_ROOT}/bsp/lvgl/lv_conf.h"
)
# Third party code: do not drown the firmware build in its -Wextra output.
target_compile_options(lvgl PRIVATE -w)

target_link_libraries(${PROJECT_NAME}.elf PRIVATE
    lvgl project_common)

project_linker_script(${PROJECT_NAME}.elf
    ${APP_ROOT}/bsp/platform/STM32F103ZETX_FLASH.ld)
set_target_properties(${PROJECT_NAME}.elf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_options(${PROJECT_NAME}.elf PRIVATE
    -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf>
            ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
)
