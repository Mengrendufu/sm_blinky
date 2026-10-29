cmake_minimum_required(VERSION 3.25)
project(bootloader LANGUAGES C ASM)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
if(NOT TARGET project_common)
    add_subdirectory(${REPO_ROOT}/project_common
                     ${CMAKE_BINARY_DIR}/project_common)
endif()

set(ROOT_CMSIS_CORE ${REPO_ROOT}/3rd_party/cmsis-core)
set(ROOT_CMSIS_DEVICE ${REPO_ROOT}/3rd_party/cmsis-device-f1)
set(ROOT_HAL ${REPO_ROOT}/3rd_party/stm32f1xx-hal-driver)

# Preserve the original HAL source selection during project extraction.
set(HAL_MODULES
    hal hal_cortex hal_dma hal_exti hal_flash hal_flash_ex
    hal_gpio hal_gpio_ex hal_pwr hal_rcc hal_rcc_ex hal_tim hal_tim_ex
    hal_uart hal_spi hal_sram ll_fsmc
)
set(SRC_HAL)
foreach(module IN LISTS HAL_MODULES)
    list(APPEND SRC_HAL ${ROOT_HAL}/Src/stm32f1xx_${module}.c)
endforeach()

add_executable(bootloader.elf
    application/bl_main.c
    application/bl_boot.c
    diagnostics/bl_fault_log.c
    diagnostics/bl_fault_report.c
    link/bl_link_tx.c
    ${REPO_ROOT}/3rd_party/sm_hsm/src/sm_hsm.c
    bsp/platform/stm32f1xx_it.c
    bsp/platform/stm32f1xx_hal_msp.c
    bsp/platform/stm32f1xx_hal_timebase_tim.c
    bsp/platform/syscalls.c
    bsp/platform/sysmem.c
    ${ROOT_CMSIS_DEVICE}/Source/Templates/system_stm32f1xx.c
    ${ROOT_CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32f103xe.s
    ${SRC_HAL}
)

target_include_directories(bootloader.elf PRIVATE
    bsp bsp/platform diagnostics link ports
    ${REPO_ROOT}/3rd_party/sm_hsm/include
    ${ROOT_CMSIS_CORE}/CMSIS/Core/Include
    ${ROOT_CMSIS_DEVICE}/Include
    ${ROOT_HAL}/Inc
    ${ROOT_HAL}/Inc/Legacy
)

target_link_libraries(bootloader.elf PRIVATE project_common)
project_linker_script(bootloader.elf
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/platform/stm32f1xx_bl.ld)
set_target_properties(bootloader.elf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_options(bootloader.elf PRIVATE
    -Wl,-Map=${CMAKE_BINARY_DIR}/bootloader.map
)
add_custom_command(TARGET bootloader.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:bootloader.elf>
            ${CMAKE_BINARY_DIR}/bootloader.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:bootloader.elf>
)
