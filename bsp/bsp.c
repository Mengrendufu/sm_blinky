//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "sst.h"
#include "dbc_assert.h"
// #include "stm32f1xx_hal_gpio.h"
#include "bsp.h"
#include "bsp_lcd.h"
#include "bsp_sram.h"
#include "bsp_flash.h"
#include "timers.h"
// #include "stm32f103xe.h"

//============================================================================
// Trace channel. Building it here, before the scheduler, is safe and always
// has been: SM_Tracer_ctor is plain RAM writes, no critical section anywhere.
// *Pushing* is the part that cannot happen yet - SM_Trace_getBlock and
// SM_Trace_push enter a FreeRTOS critical section (ports/sm_port.h), and
// before the scheduler its nesting counter still holds the sentinel, so
// interrupts stay masked from then on. That is why every record now waits for
// a task (Application_dbgBootReport, from Blinky_init) and bring-up keeps its
// own facts instead (BSP_dbgBootMark / BSP_dbgBootReport, the panel's and the
// asset's own deferred reports). Pool and queue are file statics: no
// The main stack's watermark. __get_MSP() inside the tick hook only ever
// showed the tick's own frame (96 bytes on the glass, 2026-10-13): the deep
// excursions happen in other interrupts, and no sample point can see them.
// Painting can: everything below the current stack pointer is by definition
// unused, so it can be filled with a pattern, and whatever is still painted
// later is what the stack never reached.
extern uint8_t _estack;
extern uint32_t _Min_Stack_Size;

#define BSP_MSP_PATTERN 0x5A5A5A5AU

static uint8_t *Bsp_mspBase_;    // bottom of the reserved region
static uint8_t *Bsp_mspPainted_; // highest painted byte
static bool Bsp_mspReady_;

void BSP_mspPaint(void) {
    uint32_t *p;
    // Everything below the current stack pointer is unused by definition, so
    // it is safe to write to. The pointer is rounded down to a word boundary:
    // only whole words are painted and only whole words are scanned.
    uint32_t * const sp = (uint32_t *)(__get_MSP() & ~0x7U);

    Bsp_mspBase_ = (uint8_t *)&_estack - (uint32_t)&_Min_Stack_Size;
    for (p = (uint32_t *)Bsp_mspBase_; p < sp; ++p) {
        *p = BSP_MSP_PATTERN;
    }
    Bsp_mspPainted_ = (uint8_t *)(sp - 1U);
    Bsp_mspReady_ = true;
}

uint32_t BSP_mspPeakBytes(void) {
    uint32_t const *p = (uint32_t const *)Bsp_mspBase_;

    if (!Bsp_mspReady_) {
        return 0U;
    }

    while (((uint8_t const *)p < Bsp_mspPainted_)
           && (*p == BSP_MSP_PATTERN)) {
        ++p;
    }

    return (uint32_t)((uint8_t const *)Bsp_mspPainted_
                      - (uint8_t const *)p);
}

// allocation.
void BSP_Tracer_init(void) {
    static bool done;

    if (done) {
        return;
    }
    done = true;

    //........................................................................
    //........................................................................
    // SM_Trace initialization......
    struct SM_Tracer_Frame {
        void *sto[ (SM_TRACER_LEN-1) / sizeof(void*) + 1];
    };
    static struct SM_Tracer_Frame SM_Tracer_pSto[16];
    static void *SM_Tracer_qSto[16];
    SM_Tracer_ctor(
        &SM_Tracer_inst,
        (void *)SM_Tracer_pSto,
        sizeof(SM_Tracer_pSto),
        sizeof(SM_Tracer_pSto[0]),
        (void *)SM_Tracer_qSto,
        sizeof(SM_Tracer_qSto) / sizeof(SM_Tracer_qSto[0]));
}
//............................................................................
//============================================================================
// == DEBUG: boot phase timing ==
// The replacement for the records a deferred tracer cannot
// push: four marks, three deltas, reported as one record once the tracer is
// up. Before the move above the tick was frozen from the first record on, so
// this is also the check that the boot clock works at all.
#define BSP_DBG_BOOT_MARKS 4U

static uint32_t BSP_dbgBootMs_[BSP_DBG_BOOT_MARKS];
static uint8_t BSP_dbgBootMarkIdx_;

void BSP_dbgBootMark(void) {
    if (BSP_dbgBootMarkIdx_ < BSP_DBG_BOOT_MARKS) {
        BSP_dbgBootMs_[BSP_dbgBootMarkIdx_] = HAL_GetTick();
        ++BSP_dbgBootMarkIdx_;
    }
}
//............................................................................
// The three deltas in milliseconds, two bytes each: panel bring-up, asset
// load, and everything from there to the scheduler.
void BSP_dbgBootReport(void) {
    uint32_t const panel = BSP_dbgBootMs_[1] - BSP_dbgBootMs_[0];
    uint32_t const asset = BSP_dbgBootMs_[2] - BSP_dbgBootMs_[1];
    uint32_t const rest = BSP_dbgBootMs_[3] - BSP_dbgBootMs_[2];

    // A fault from a previous life leaves its frame in the cross reset area
    // (bsp/platform/stm32f1xx_it.c). Reported here rather than on the way up:
    // the tracer does not exist until a task runs, and a fault is otherwise
    // silent - two LED blinks, a reset, nothing on the wire.
    {
        uint32_t * const s = BSP_dbgFaultStash();

        if ((s[0] | s[1] | s[3]) != 0U) {
            // 66: the instruction that faulted, 4 bytes, then the fault's own
            // report, 2 bytes. 67: where it was going (LR) and HFSR.
            SM_TRACE_BEGIN(66U, 6)
                SM_TRACE_PAYLOAD_BE((uint8_t)((s[0] >> 24) & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)((s[0] >> 16) & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)((s[0] >> 8) & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)(s[0] & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)((s[3] >> 8) & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)(s[3] & 0xFFU))
            SM_TRACE_END()

            SM_TRACE_BEGIN(67U, 6)
                SM_TRACE_PAYLOAD_BE((uint8_t)((s[1] >> 24) & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)((s[1] >> 16) & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)((s[1] >> 8) & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)(s[1] & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)((s[4] >> 8) & 0xFFU))
                SM_TRACE_PAYLOAD_BE((uint8_t)(s[4] & 0xFFU))
            SM_TRACE_END()

            s[0] = 0U;
            s[1] = 0U;
            s[3] = 0U;
        }
    }

    SM_TRACE_BEGIN(79U, 6)
        SM_TRACE_PAYLOAD_BE((uint8_t)((panel >> 8) & 0xFFU))
        SM_TRACE_PAYLOAD_BE((uint8_t)(panel & 0xFFU))
        SM_TRACE_PAYLOAD_BE((uint8_t)((asset >> 8) & 0xFFU))
        SM_TRACE_PAYLOAD_BE((uint8_t)(asset & 0xFFU))
        SM_TRACE_PAYLOAD_BE((uint8_t)((rest >> 8) & 0xFFU))
        SM_TRACE_PAYLOAD_BE((uint8_t)(rest & 0xFFU))
    SM_TRACE_END()
}
//............................................................................
void BSP_init(void) {
    // The channel itself first, as it always was: the constructor touches no
    // critical section. Using it is what waits for the scheduler.
    BSP_Tracer_init();

    SystemCoreClockUpdate(); // Update SystemCoreClock

    // Board bring-up: everything has to be ready before a task touches it,
    // and the panel wants its sleep-out wait over before anyone draws into
    // it, so this runs before the scheduler exists. Contract: BSP_init()
    // returns with the panel out of sleep and black, both banks ready and
    // the SPI flash probed.
    BSP_lcdInit();
    BSP_sramInit();
    BSP_flashInit();
}
//............................................................................
// Stack headroom of the three tasks that always exist. The values are the
// minimum free stack ever seen, in words (1 word = 4 B); they are the numbers
// the static stack sizes get sized from.
static void BSP_dbgTraceStack_(uint8_t recId, uint16_t words) {
    SM_TRACE_BEGIN(recId, 2)
        SM_TRACE_PAYLOAD_BE(words >> 8)
        SM_TRACE_PAYLOAD_BE(words)
    SM_TRACE_END()
}
//............................................................................
void BSP_dbgMemReport(uint8_t selfRecId) {
    TaskHandle_t const idle = xTaskGetIdleTaskHandle();
    TaskHandle_t const timer = xTimerGetTimerDaemonTaskHandle();

    BSP_dbgTraceStack_(selfRecId,
                    (uint16_t)uxTaskGetStackHighWaterMark((TaskHandle_t)0));
    BSP_dbgTraceStack_(BSP_TRACE_STACK_IDLE,
                    (uint16_t)uxTaskGetStackHighWaterMark(idle));
    BSP_dbgTraceStack_(BSP_TRACE_STACK_TIMER,
                    (uint16_t)uxTaskGetStackHighWaterMark(timer));
}
//............................................................................
void BSP_ledOn(void) {
    // The LED is active low: logic 0 lights it.
    HAL_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, 0);
}
void BSP_ledOff(void) {
    HAL_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, 1);
}

/*==========================================================================*/
void SM_Tracer_flush_byte_(unsigned char byte_) {
    extern UART_HandleTypeDef huart1;
    static unsigned char pdata[1];
    pdata[0] = byte_;
    HAL_UART_Transmit(&huart1, pdata, 1, HAL_MAX_DELAY);
}

void SM_Tracer_flushByte_(unsigned char byte_) {
    SM_Tracer_flush_byte_(byte_);
}

/*==========================================================================*/
SM_NORETURN SM_onAssert(char const *module, int label) {
    //========================================================================
    uint8_t tmp;
    unsigned char *pMdl;
    uint8_t chksum;
    uint8_t escByte;

    //........................................................................
    pMdl = (unsigned char *)module;
    escByte = 0x7D;
    chksum = 0;

    // Silent system..........................................................
    __disable_irq();

    // Peripheral clean ......................................................
    extern UART_HandleTypeDef huart1;
    huart1.gState = HAL_UART_STATE_READY;
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET);
    __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC);

    // Flag...................................................................
    tmp = 0x7E; SM_Tracer_flush_byte_(tmp);
    // Seq (static) ..........................................................
    tmp = 233; SM_Tracer_flush_byte_(tmp); chksum += tmp;
    // RecID (static) ........................................................
    tmp = 0; SM_Tracer_flush_byte_(tmp); chksum += tmp;
    // pldLen (static) .......................................................
    tmp = 4; // sizeof label.
    while (*module++) ++tmp; // sizeof char * module
    SM_Tracer_flush_byte_(tmp); chksum += tmp;
    // label .................................................................
    tmp = (unsigned char)(label >> 24); chksum += tmp;
    if (tmp == 0x7E || tmp == 0x7D) {
        SM_Tracer_flush_byte_(escByte);
        SM_Tracer_flush_byte_(tmp ^ 0x20);
    } else {
        SM_Tracer_flush_byte_(tmp);
    }
    tmp = (unsigned char)(label >> 16); chksum += tmp;
    if (tmp == 0x7E || tmp == 0x7D) {
        SM_Tracer_flush_byte_(escByte);
        SM_Tracer_flush_byte_(tmp ^ 0x20);
    } else {
        SM_Tracer_flush_byte_(tmp);
    }
    tmp = (unsigned char)(label >>  8); chksum += tmp;
    if (tmp == 0x7E || tmp == 0x7D) {
        SM_Tracer_flush_byte_(escByte);
        SM_Tracer_flush_byte_(tmp ^ 0x20);
    } else {
        SM_Tracer_flush_byte_(tmp);
    }
    tmp = (unsigned char)(label >>  0); chksum += tmp;
    if (tmp == 0x7E || tmp == 0x7D) {
        SM_Tracer_flush_byte_(escByte);
        SM_Tracer_flush_byte_(tmp ^ 0x20);
    } else {
        SM_Tracer_flush_byte_(tmp);
    }
    // module ................................................................
    while (*pMdl) {
        tmp = *pMdl++; chksum += tmp;
        if (tmp == 0x7E || tmp == 0x7D) {
            SM_Tracer_flush_byte_(escByte);
            SM_Tracer_flush_byte_(tmp ^ 0x20);
        } else {
            SM_Tracer_flush_byte_(tmp);
        }
    }
    // chksum ................................................................
    tmp = ~chksum;
    if (tmp == 0x7E || tmp == 0x7D) {
        SM_Tracer_flush_byte_(escByte);
        SM_Tracer_flush_byte_(tmp ^ 0x20);
    } else {
        SM_Tracer_flush_byte_(tmp);
    }
    // Flag...................................................................
    tmp = 0x7E; SM_Tracer_flush_byte_(tmp);

    // Reset system. .........................................................
    NVIC_SystemReset();
}
//............................................................................
DBC_NORETURN void DBC_fault_handler(char const * module, int label) {
    SM_onAssert(module, label);
}

//============================================================================
void SST_onStart(void) {
}
//............................................................................
// One idle pass, never a loop: the loop belongs to the caller. On this port
// that is the FreeRTOS idle task (vApplicationIdleHook() runs once per
// iteration of its own loop); on the stand-alone SST port it is the kernel's
// idle loop. Returning also lets the idle task run its bookkeeping.
void SST_onIdle(void) {
    // __WFI();
    SM_Tracer_idle(&SM_Tracer_inst);
}
