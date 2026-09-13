# sm_blinky

STM32F103ZET6 blinky using FreeRTOS, `sm_sst`, and `sm_hsm`.

## Checkout

```sh
git clone --recurse-submodules <repository-url>
```

For an existing checkout:

```sh
git submodule update --init --recursive
```

The ST components are pinned to a documented compatible set:

- STM32F1 HAL Driver `v1.1.8`
- CMSIS Device F1 `v4.3.3`
- CMSIS Core `5.4.0`

## Build

Arm GNU Toolchain, CMake, and Ninja must be available on `PATH`.

```sh
cmake --preset debug
cmake --build --preset build-debug
```

The ELF, HEX, and map files are generated under `build/debug/`.
