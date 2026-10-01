set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

set(MCU_ARCH_FLAGS "-mcpu=cortex-m3 -mthumb -mfloat-abi=soft")
set(MCU_MACRO_FLAGS "-DSTM32F103xE -DUSE_HAL_DRIVER")

set(CMAKE_C_FLAGS
    "${MCU_ARCH_FLAGS} ${MCU_MACRO_FLAGS} -O2 -Wall -Wextra -g -ffunction-sections -fdata-sections"
    CACHE STRING "C flags"
    FORCE)
set(CMAKE_ASM_FLAGS
    "${MCU_ARCH_FLAGS} ${MCU_MACRO_FLAGS} -x assembler-with-cpp"
    CACHE STRING "ASM flags"
    FORCE)
set(CMAKE_EXE_LINKER_FLAGS
    "${MCU_ARCH_FLAGS} --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections"
    CACHE STRING "Linker flags"
    FORCE)
