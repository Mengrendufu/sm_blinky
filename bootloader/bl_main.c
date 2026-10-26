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
// Asset protocol and NOR verification are implemented below.
//============================================================================
#include "stm32f1xx_hal.h"
#include "bsp_flash.h"
#include "asset_format.h"
#include "nor_partitions.h"
#include "sm_assert.h"
#include "sm_tracer.h"

#include <stdbool.h>
#include <string.h>

//============================================================================
// The flash driver traces through the shared macros, and the tracer's block
// allocator enters a FreeRTOS critical section - something this program cannot
// do: there is no scheduler here, and the pre-scheduler latch would leave
// interrupts masked for good. So sm_tracer.c is not linked into this image;
// these two stubs keep the exact signatures, which turns every SM_TRACE_* in
// bsp_flash.c into nothing.
unsigned char *SM_Trace_getBlock(SM_Tracer * const me) {
    (void)me;

    return (unsigned char *)0;
}
//............................................................................
void SM_Trace_push(SM_Tracer * const me, unsigned char *pfb) {
    (void)me;
    (void)pfb;
}

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
#define BL_PROTO_VER   ASSET_FORMAT_VERSION
#define BL_BLOCK_BYTES 4096U
#define BL_ASSET_MAX   ASSET_FORMAT_MAX_PACKAGE
#define BL_ASSET_BASE  NOR_ASSET_BASE
#define BL_ASSET_DATA  NOR_ASSET_DATA
#define BL_HDR_BYTES   ASSET_FORMAT_HEADER
#define BL_PAGE_BYTES  256U         // the device's program page

_Static_assert(BL_ASSET_BASE + BL_ASSET_MAX == NOR_ASSET_END,
               "bootloader and NOR asset ranges disagree");

// Protocol v2 transfers a directory sector followed by fixed-size images.
// Block zero is staged in RAM; NOR directory is invalidated before payload
// writes, then committed only after every stored image passes its own CRC.
// The final package CRC also protects the complete transport stream.
//
//   block    PC -> BL:  B1 | idx(2 BE) | len(2 BE) | crc32低16(2 BE)   7 B
//                       then len bytes of payload
//   final    PC -> BL:  C1 | total(4 BE) | crc32(4 BE)                  9 B
//   answer   BL -> PC:  55 (taken) / 4E (rejected)
#define BL_BLK_MAGIC   0xB1U
#define BL_BLK_HDR     7U
#define BL_FIN_MAGIC   0xC1U
#define BL_FIN_LEN     9U
#define BL_MAX_BLOCK   4096U
#define BL_POLL_MS     20U          // how long to wait for the next byte

// Written by J-Link, cleared here. Same address in both images (see the .ld).
extern uint32_t _snoinit;

// Burn-mode counters. Global on purpose: a J-Link probe reads them by name
// (arm-none-eabi-nm gives the addresses) while the target keeps running.
uint32_t Bl_frames_;    // frames that started with the marker byte
uint32_t Bl_ok_;        // frames that passed the CRC
uint32_t Bl_bad_;       // frames rejected: version, length or CRC
uint32_t Bl_len_;       // length field of the last accepted frame
uint32_t Bl_blocks_;    // blocks taken (payload CRC matched)
uint32_t Bl_retry_;     // blocks rejected - the PC is expected to send again
uint32_t Bl_bytes_;     // payload bytes folded into the running CRC
uint32_t Bl_crcRun_;    // running CRC32 over the payload (folded per good block)
uint32_t Bl_final_;     // 0 = no final frame yet, 1 = matched, 2 = mismatched
uint32_t Bl_last_;      // index of the last block taken
uint32_t Bl_written_;   // accepted blocks (directory staged, data verified)
uint32_t Bl_wrBad_;     // blocks whose erase/program/read-back failed

// Stop-and-wait retransmission acknowledges the last accepted block without
// writing it or folding it into the transport CRC twice.
static uint8_t Bl_block_[BL_MAX_BLOCK];
static uint8_t Bl_page_[BL_PAGE_BYTES];      // read-back bytes
static uint8_t Bl_directory_[BL_HDR_BYTES];
static uint32_t Bl_lastCrc_;

static UART_HandleTypeDef Bl_uart_;

//============================================================================
static void Bl_clock_(void);
static void Bl_uartInit_(void);
static void Bl_say_(char const *text);
static bool Bl_appSane_(void);
static void Bl_burn_(void) __attribute__((noreturn));
static void Bl_handOver_(void) __attribute__((noreturn));
static uint32_t Bl_crc32_(uint8_t const *data, uint32_t len);
static uint32_t Bl_crc32Update_(uint32_t crc, uint8_t const *data,
                                uint32_t len);
static uint32_t Bl_be32_(uint8_t const *p);
static void Bl_putBe32_(uint8_t *p, uint32_t v);
static bool Bl_frameOk_(uint8_t const *frame);
static bool Bl_readExact_(uint8_t *dst, uint32_t len);
static bool Bl_writeBlock_(uint32_t idx, uint32_t len);
static bool Bl_commit_(void);
static void Bl_ackFrame_(void);
static void Bl_ack1_(void);
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
// A lost DMA completion cannot return a buffer that hardware may still own.
void SM_onAssert(char const *module, int label) {
    (void)module;
    (void)label;
    NVIC_SystemReset();
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
// final xor), so the PC side can use its standard library call. The update
// form exists because the payload arrives in blocks and a re-sent block must
// not be folded in twice.
static uint32_t Bl_crc32Update_(uint32_t crc, uint8_t const *data,
                                uint32_t len) {
    return AssetFormat_crcUpdate(crc, data, len);
}
//............................................................................
static uint32_t Bl_crc32_(uint8_t const *data, uint32_t len) {
    return Bl_crc32Update_(0U, data, len);
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
    if ((len <= ASSET_FORMAT_SECTOR) || (len > BL_ASSET_MAX)
        || (((len - ASSET_FORMAT_SECTOR) % ASSET_FORMAT_SLOT_BYTES)
            != 0U)) {
        return false;
    }

    return crc == Bl_crc32_(&frame[1], 5U);
}
//............................................................................
// The handshake answer: 11 bytes with its own CRC.
static void Bl_ackFrame_(void) {
    uint8_t out[BL_ACK_LEN];

    out[0] = BL_ACK_MAGIC;
    Bl_putBe32_(&out[1], BL_ASSET_BASE);
    out[5] = (uint8_t)(BL_BLOCK_BYTES >> 8);
    out[6] = (uint8_t)(BL_BLOCK_BYTES & 0xFFU);
    Bl_putBe32_(&out[7], Bl_crc32_(out, 7U));

    (void)HAL_UART_Transmit(&Bl_uart_, out, BL_ACK_LEN, 100U);
}
//............................................................................
// Block and final answers are one byte. Mixing this up with the 11-byte
// handshake frame leaves ten bytes of junk per block in the PC's input
// buffer - which is exactly what the first run of the block test hit.
static void Bl_ack1_(void) {
    uint8_t n = BL_ACK_MAGIC;

    (void)HAL_UART_Transmit(&Bl_uart_, &n, 1U, 100U);
}
//............................................................................
static void Bl_nak_(void) {
    uint8_t n = BL_NAK_MAGIC;

    (void)HAL_UART_Transmit(&Bl_uart_, &n, 1U, 100U);
}
//............................................................................
// Bare-metal wait capabilities: DMA completion is latched, then consumed
// by a busy wait. Burn mode runs in main with interrupts and HAL tick live.
typedef struct {
    volatile bool done;
} Bl_FlashWait;

static Bl_FlashWait Bl_flashWait_;

static bool Bl_flashIsCaller_(void *context) {
    return (context == &Bl_flashWait_) && (__get_IPSR() == 0U);
}

static bool Bl_flashWaitDone_(void *context, uint32_t timeoutMs) {
    Bl_FlashWait * const wait = (Bl_FlashWait *)context;
    uint32_t const started = HAL_GetTick();

    while (!wait->done) {
        if ((uint32_t)(HAL_GetTick() - started) >= timeoutMs) {
            return false;
        }
    }
    wait->done = false;
    return true;
}

static void Bl_flashNotifyDone_(void *context) {
    Bl_FlashWait * const wait = (Bl_FlashWait *)context;

    wait->done = true;
}

static void Bl_flashSleepMs_(void *context, uint32_t delayMs) {
    (void)context;
    HAL_Delay(delayMs);
}

static void Bl_flashYield_(void *context) {
    (void)context;
}

static uint32_t Bl_flashNow_(void *context) {
    (void)context;
    return HAL_GetTick();
}

static uint32_t Bl_flashElapsedMs_(void *context, uint32_t started) {
    (void)context;
    return HAL_GetTick() - started;
}

static void Bl_flashBindWait_(void) {
    static BSP_flashWaitOperations const operations = {
        .isCaller = &Bl_flashIsCaller_,
        .waitDone = &Bl_flashWaitDone_,
        .notifyDone = &Bl_flashNotifyDone_,
        .sleepMs = &Bl_flashSleepMs_,
        .yield = &Bl_flashYield_,
        .now = &Bl_flashNow_,
        .elapsedMs = &Bl_flashElapsedMs_,
    };

    BSP_flashBind(BSP_flashDevice(), &BSP_flashDmaOperations,
                   &operations, &Bl_flashWait_);
}

// Program one block into the NOR and read it back. Same bus, so the read-back
// is a check of what the device kept, not of what we think we sent.
static bool Bl_writeBlock_(uint32_t idx, uint32_t len) {
    uint32_t const addr = BL_ASSET_BASE + (idx * BL_MAX_BLOCK);
    uint32_t off;
    uint32_t n;

    if (!BSP_flashEraseSector(BSP_flashDevice(), addr)) {
        return false;
    }

    if (!BSP_flashWrite(BSP_flashDevice(), addr, Bl_block_, len)) {
        return false;
    }

    for (off = 0U; off < len; off += BL_PAGE_BYTES) {
        n = ((len - off) > BL_PAGE_BYTES) ? BL_PAGE_BYTES : (len - off);
        if (!BSP_flashRead(BSP_flashDevice(), addr + off, Bl_page_, n)) {
            return false;
        }
        if (memcmp(Bl_page_, &Bl_block_[off], n) != 0) {
            return false;
        }
    }

    return true;
}
//............................................................................
// Verify each image from NOR against the script-provided expected CRC.
// The directory sector was erased before the first payload write. This
// invalidates an interrupted replacement; it does not preserve the old pack.
static bool Bl_commit_(void) {
    uint32_t const count = AssetFormat_be32(&Bl_directory_[8]);

    for (uint32_t i = 0U; i < count; ++i) {
        uint8_t const * const entry = AssetFormat_entry(Bl_directory_, i);
        uint32_t const addr = BL_ASSET_DATA + AssetFormat_be32(&entry[4]);
        uint32_t const len = AssetFormat_be32(&entry[8]);
        uint32_t crc = 0U;

        for (uint32_t off = 0U; off < len; off += BL_PAGE_BYTES) {
            uint32_t const n = ((len - off) > BL_PAGE_BYTES)
                ? BL_PAGE_BYTES : (len - off);
            if (!BSP_flashRead(BSP_flashDevice(), addr + off, Bl_page_, n)) {
                return false;
            }
            crc = Bl_crc32Update_(crc, Bl_page_, n);
        }
        if (crc != AssetFormat_be32(&entry[12])) {
            return false;
        }
    }
    if (!BSP_flashWrite(BSP_flashDevice(), BL_ASSET_BASE,
                        Bl_directory_, BL_HDR_BYTES)
        || !BSP_flashRead(BSP_flashDevice(), BL_ASSET_BASE,
                          Bl_page_, BL_HDR_BYTES)) {
        return false;
    }
    return memcmp(Bl_page_, Bl_directory_, BL_HDR_BYTES) == 0;
}
//............................................................................
// Read exactly len bytes, one at a time. Returns false on the first gap: the
// caller answers NAK and the PC sends the block again.
static bool Bl_readExact_(uint8_t *dst, uint32_t len) {
    uint32_t i;

    for (i = 0U; i < len; ++i) {
        if (HAL_UART_Receive(&Bl_uart_, &dst[i], 1U, BL_POLL_MS) != HAL_OK) {
            return false;
        }
    }

    return true;
}
//............................................................................
// Burn mode. Three kinds of frame arrive here, told apart by their first byte:
// the handshake (AA), a payload block (B1) and the final whole-image CRC (C1).
// Writes a complete package; single-image updates are not supported.
static void Bl_burn_(void) {
    // One buffer for all three frame kinds: the handshake is the longest at
    // BL_REQ_LEN, so a block header fits with room to spare. (Sizing this to
    // BL_BLK_HDR instead overflowed the stack by three bytes - the handshake
    // read 10 bytes into a 7-byte array.)
    uint8_t hdr[BL_REQ_LEN];
    uint8_t fin[BL_FIN_LEN];
    uint8_t b;
    uint32_t idx;
    uint32_t len;
    uint32_t lo16;

    Bl_say_("[bootloader] burn mode: handshake, blocks, then the header\r\n");
    BSP_flashInit();
    Bl_flashBindWait_();

    for (;;) {
        // Polling: burn mode has nothing else to do and the app is not
        // running, so a byte missed here is lost for good.
        if (HAL_UART_Receive(&Bl_uart_, &b, 1U, BL_POLL_MS) != HAL_OK) {
            continue;
        }

        if (b == BL_REQ_MAGIC) {
            hdr[0] = b;
            if (!Bl_readExact_(&hdr[1], BL_REQ_LEN - 1U)) {
                continue;
            }
            ++Bl_frames_;
            if (!Bl_frameOk_(hdr)) {
                ++Bl_bad_;
                Bl_nak_();
                continue;
            }
            if ((Bl_bytes_ != 0U)
                && (Bl_len_ != Bl_be32_(&hdr[2]))) {
                Bl_nak_();
                continue;
            }
            ++Bl_ok_;
            Bl_len_ = Bl_be32_(&hdr[2]);
            Bl_ackFrame_();
            continue;
        }

        if (b == BL_BLK_MAGIC) {
            hdr[0] = b;
            if (!Bl_readExact_(&hdr[1], BL_BLK_HDR - 1U)) {
                continue;
            }
            idx = ((uint32_t)hdr[1] << 8) | (uint32_t)hdr[2];
            len = ((uint32_t)hdr[3] << 8) | (uint32_t)hdr[4];
            lo16 = ((uint32_t)hdr[5] << 8) | (uint32_t)hdr[6];

            if ((len == 0U) || (len > BL_MAX_BLOCK)
                || !Bl_readExact_(Bl_block_, len)) {
                ++Bl_retry_;
                Bl_nak_();
                continue;
            }
            if ((Bl_crc32_(Bl_block_, len) & 0xFFFFU) != lo16) {
                ++Bl_retry_;
                Bl_nak_();
                continue;
            }

            // Bound index/length to the negotiated complete package. The
            // last accepted block may be replayed after a lost ACK.
            uint32_t const offset = idx * BL_MAX_BLOCK;
            uint32_t const crc = Bl_crc32_(Bl_block_, len);
            if ((Bl_len_ == 0U) || (offset >= Bl_len_)
                || (len != (((Bl_len_ - offset) > BL_MAX_BLOCK)
                    ? BL_MAX_BLOCK : (Bl_len_ - offset)))) {
                Bl_nak_();
                continue;
            }
            if ((Bl_bytes_ != 0U) && (idx == Bl_last_)
                && (crc == Bl_lastCrc_)) {
                Bl_ack1_();
                continue;
            }
            if (offset != Bl_bytes_) {
                Bl_nak_();
                continue;
            }
            if (idx == 0U) {
                if (!AssetFormat_valid(Bl_block_)
                    || (AssetFormat_be32(&Bl_block_[12])
                        + ASSET_FORMAT_SECTOR != Bl_len_)) {
                    Bl_nak_();
                    continue;
                }
                if (!BSP_flashEraseSector(BSP_flashDevice(), BL_ASSET_BASE)) {
                    Bl_nak_();
                    continue;
                }
                memcpy(Bl_directory_, Bl_block_, BL_HDR_BYTES);
            } else if (!Bl_writeBlock_(idx, len)) {
                ++Bl_wrBad_;
                Bl_nak_();
                continue;
            }

            // Folded in only after the write: a block that failed its CRC or
            // its write must not reach the running CRC, or the final check
            // could never match.
            Bl_crcRun_ = Bl_crc32Update_(Bl_crcRun_, Bl_block_, len);
            Bl_bytes_ += len;
            Bl_last_ = idx;
            Bl_lastCrc_ = crc;
            ++Bl_blocks_;
            ++Bl_written_;
            Bl_ack1_();
            continue;
        }

        if (b == BL_FIN_MAGIC) {
            fin[0] = b;
            if (!Bl_readExact_(&fin[1], BL_FIN_LEN - 1U)) {
                continue;
            }
            if ((Bl_len_ == 0U) || (Bl_bytes_ != Bl_len_)
                || (Bl_be32_(&fin[1]) != Bl_len_)
                || (Bl_be32_(&fin[5]) != Bl_crcRun_)) {
                Bl_final_ = 2U;
                Bl_nak_();
                continue;
            }
            // Transport complete: verify stored images, then commit AST2.
            if (Bl_commit_()) {
                Bl_final_ = 1U;
                _snoinit = 0U;
                Bl_ack1_();
                // The work is done and the flag is gone: reset so the board
                // comes up in the app on its own.
                HAL_Delay(50U);
                NVIC_SystemReset();
            } else {
                Bl_final_ = 3U;
                Bl_nak_();
            }
            continue;
        }

        // Anything else: noise between frames, drop it.
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
