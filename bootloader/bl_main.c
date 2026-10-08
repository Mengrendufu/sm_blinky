//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
//============================================================================
//=== Bootloader -- the first 16 KB of the flash
//
// It owns two things:
//
//   * The cross-reset flag. The linker script reserves the top 32 bytes of RAM
//     (_snoinit) and the startup code does not clear them, so a word survives
//     a reset (not a power cycle). Writing 0xB00710AD there asks for the next
//     reset to land here instead of in the app: J-Link writes it from the PC
//     (tools/enter-burn.jlink.tpl), and this program clears it when the work
//     is done.
//   * The decision. Flag set, or a handshake inside the boot window, means the
//     board stays here and serves the asset protocol (the receive state machine
//     is docs/diagrams/uart-recv-fsm.mmd; the conversation is
//     docs/diagrams/asset-burn-bootloader.mmd). Otherwise the app gets the CPU.
//
// The hand-over is the part that has to be right. On a real reset the hardware
// loads MSP and PC out of the vector table; nothing does that for a jump we
// make ourselves, so VTOR, MSP and PC are set by hand below.
//
// Not here yet: the protocol itself (the skeleton stops in burn mode and says
// so on the wire), and the NOR flash work that needs it.
//============================================================================
#include "stm32f1xx_hal.h"

#include <stdbool.h>
#include <string.h>

//============================================================================
// What the two linker scripts and the app's main() agree on.
#define BL_APP_BASE    0x08004000U  // app image: flash + 16 KB
#define BL_FLASH_END   0x08080000U  // 512 KB part, one past the last byte
#define BL_RAM_BASE    0x20000000U
#define BL_RAM_END     0x20010000U  // 64 KB, _estack sits 32 bytes below

#define BL_FLAG_MAGIC  0xB00710ADU
#define BL_BOOT_MS     200U         // handshake window for the RESET key

// Burn-mode handshake (step 3 of the plan: prove the wire, touch no flash).
//
//   request  PC -> BL:  AA | ver(1) | len(4 BE) | crc32(4 BE)     10 bytes
//                       crc32 covers ver+len (5 bytes)
//   reply    BL -> PC:  55 | base(4 BE) | block(2 BE) | crc32(4 BE)  12 bytes
//                       crc32 covers the first 7 bytes, so the PC can tell a
//                       real answer from noise on a line the trace also drives
//   refusal  BL -> PC:  4E
#define BL_REQ_MAGIC   0xAAU
#define BL_ACK_MAGIC   0x55U
#define BL_NAK_MAGIC   0x4EU
#define BL_REQ_LEN     10U
#define BL_ACK_LEN     11U   // 55 + base(4) + block(2) + crc32(4)
#define BL_PROTO_VER   1U
#define BL_BLOCK_BYTES 4096U
#define BL_ASSET_MAX   0x00040000U  // sanity bound on the length field
#define BL_ASSET_BASE  0x00081000U  // NOR assets area (charter partition plan)

// Written by J-Link, cleared here. Same address in both images (see the .ld).
extern uint32_t _snoinit;

// Burn-mode counters. Global on purpose: a J-Link probe reads them by name
// (arm-none-eabi-nm gives the addresses) while the target keeps running.
uint32_t Bl_frames_;    // frames that started with the marker byte
uint32_t Bl_ok_;        // frames that passed the CRC
uint32_t Bl_bad_;       // frames rejected: version, length or CRC
uint32_t Bl_len_;       // length field of the last accepted frame

static UART_HandleTypeDef Bl_uart_;

//============================================================================
static void Bl_clock_(void);
static void Bl_uartInit_(void);
static void Bl_say_(char const *text);
static bool Bl_appSane_(void);
static void Bl_burn_(void) __attribute__((noreturn));
static void Bl_handOver_(void) __attribute__((noreturn));
static uint32_t Bl_crc32_(uint8_t const *data, uint32_t len);
static uint32_t Bl_be32_(uint8_t const *p);
static void Bl_putBe32_(uint8_t *p, uint32_t v);
static bool Bl_frameOk_(uint8_t const *frame);
static void Bl_ack_(void);
static void Bl_nak_(void);

//============================================================================
int main(void) {
    HAL_Init();
    Bl_clock_();
    Bl_uartInit_();

    Bl_say_("\r\n[bootloader] up\r\n");

    if (_snoinit == BL_FLAG_MAGIC) {
        Bl_burn_();
    }

    // No flag: a normal start. The window gives whoever pressed the board's
    // RESET key a chance to get in. 200 ms against a cold start that already
    // spends 3~4 s on the panel is nothing.
    HAL_Delay(BL_BOOT_MS);

    if (!Bl_appSane_()) {
        Bl_say_("[bootloader] app vector table looks wrong: staying put\r\n");
        for (;;) {
        }
    }

    Bl_say_("[bootloader] handing over to the app\r\n");
    Bl_handOver_();
}

//============================================================================
void Error_Handler(void) {
    __disable_irq();

    for (;;) {
    }
}
//............................................................................
// The TIM1 time base (bsp/platform/stm32f1xx_hal_timebase_tim.c) starts the
// timer and enables the interrupt, but the callback that actually feeds
// HAL_IncTick() has to come from the image: the HAL's own is an empty weak
// one. In the app it lives in application/main.c; a bootloader that links that
// file nowhere needs its own, or every HAL_Delay() here waits forever on a
// uwTick that never moves.
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM1) {
        HAL_IncTick();
    }
}
//............................................................................
// Same clock the app runs at: 72 MHz from HSE + PLL x9, with the two wait
// states 72 MHz needs. Anything else here would change the UART baud rate
// under the PC's feet.
static void Bl_clock_(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                    | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}
//............................................................................
// TX and RX: the app only traces (TX), this side has to listen. The CH340's
// control lines stay deassert unless a tool asserts them, and the PC side is
// expected to keep them that way (AGENTS.md, "CH340C 自动下载支路的坑").
static void Bl_uartInit_(void) {
    Bl_uart_.Instance = USART1;
    Bl_uart_.Init.BaudRate = 115200;
    Bl_uart_.Init.WordLength = UART_WORDLENGTH_8B;
    Bl_uart_.Init.StopBits = UART_STOPBITS_1;
    Bl_uart_.Init.Parity = UART_PARITY_NONE;
    Bl_uart_.Init.Mode = UART_MODE_TX_RX;
    Bl_uart_.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    Bl_uart_.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&Bl_uart_) != HAL_OK) {
        Error_Handler();
    }
}
//............................................................................
static void Bl_say_(char const *text) {
    (void)HAL_UART_Transmit(&Bl_uart_, (uint8_t *)(uintptr_t)text,
                            (uint16_t)strlen(text), 100U);
}
//............................................................................
// CRC32, the same one zlib.crc32() computes (poly 0xEDB88320, init all ones,
// final xor), so the PC side can use its standard library call.
static uint32_t Bl_crc32_(uint8_t const *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i;
    uint8_t bit;

    for (i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 1U) != 0U) ? ((crc >> 1) ^ 0xEDB88320U)
                                     : (crc >> 1);
        }
    }

    return crc ^ 0xFFFFFFFFU;
}
//............................................................................
static uint32_t Bl_be32_(uint8_t const *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
           | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
//............................................................................
static void Bl_putBe32_(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
//............................................................................
static bool Bl_frameOk_(uint8_t const *frame) {
    uint32_t const ver = frame[1];
    uint32_t const len = Bl_be32_(&frame[2]);
    uint32_t const crc = Bl_be32_(&frame[6]);

    if (ver != BL_PROTO_VER) {
        return false;
    }
    if ((len == 0U) || (len > BL_ASSET_MAX)) {
        return false;
    }

    return crc == Bl_crc32_(&frame[1], 5U);
}
//............................................................................
static void Bl_ack_(void) {
    uint8_t out[BL_ACK_LEN];

    out[0] = BL_ACK_MAGIC;
    Bl_putBe32_(&out[1], BL_ASSET_BASE);
    out[5] = (uint8_t)(BL_BLOCK_BYTES >> 8);
    out[6] = (uint8_t)(BL_BLOCK_BYTES & 0xFFU);
    Bl_putBe32_(&out[7], Bl_crc32_(out, 7U));

    (void)HAL_UART_Transmit(&Bl_uart_, out, BL_ACK_LEN, 100U);
}
//............................................................................
static void Bl_nak_(void) {
    uint8_t n = BL_NAK_MAGIC;

    (void)HAL_UART_Transmit(&Bl_uart_, &n, 1U, 100U);
}
//............................................................................
// Burn mode, handshake only: collect one request frame, check it, answer it.
// Nothing touches the NOR here on purpose - the point of this step is the wire.
static void Bl_burn_(void) {
    uint8_t frame[BL_REQ_LEN];
    uint8_t b;
    uint32_t count = 0U;

    Bl_say_("[bootloader] burn mode: waiting for a handshake\r\n");

    for (;;) {
        // Polling, 20 ms per byte: burn mode has nothing else to do and the
        // app is not running, so a byte missed here is lost for good.
        if (HAL_UART_Receive(&Bl_uart_, &b, 1U, 20U) != HAL_OK) {
            continue;
        }

        if ((count == 0U) && (b != BL_REQ_MAGIC)) {
            continue;               // resynchronise on the frame marker
        }

        frame[count] = b;
        ++count;
        if (count < BL_REQ_LEN) {
            continue;
        }

        count = 0U;
        ++Bl_frames_;

        if (!Bl_frameOk_(frame)) {
            ++Bl_bad_;
            Bl_nak_();
            continue;
        }

        ++Bl_ok_;
        Bl_len_ = Bl_be32_(&frame[2]);
        Bl_ack_();
    }
}
//............................................................................
// Cheap sanity check on the image we are about to hand the CPU to. Both words
// matter: a stack pointer outside RAM, or a reset vector with the Thumb bit
// clear, faults on the first instruction fetch, and a debugger is then the
// only way back in.
static bool Bl_appSane_(void) {
    uint32_t const sp = *(uint32_t const *)BL_APP_BASE;
    uint32_t const pc = *(uint32_t const *)(BL_APP_BASE + 4U);

    if ((sp < BL_RAM_BASE) || (sp > BL_RAM_END) || ((sp & 0x3U) != 0U)) {
        return false;
    }
    if ((pc < BL_APP_BASE) || (pc >= BL_FLASH_END) || ((pc & 0x1U) == 0U)) {
        return false;
    }

    return true;
}
//............................................................................
static void Bl_handOver_(void) {
    uint32_t const sp = *(uint32_t const *)BL_APP_BASE;
    uint32_t const pc = *(uint32_t const *)(BL_APP_BASE + 4U);
    uint8_t i;

    __disable_irq();

    // Give the peripherals back the way the app expects to find them at a cold
    // start: this program opened UART1 and the TIM1 time base, and left no IRQ
    // pending. HAL_DeInit() also takes the SysTick clock off `uwTick`, so the
    // app starts its own time base from zero.
    HAL_UART_DeInit(&Bl_uart_);
    HAL_DeInit();

    for (i = 0U; i < 8U; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    SCB->VTOR = BL_APP_BASE;
    __set_MSP(sp);
    __enable_irq();
    ((void (*)(void))pc)();

    for (;;) {      // the app never returns
    }
}
