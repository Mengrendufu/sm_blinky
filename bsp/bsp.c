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
// the Flash AO first event (Application_dbgBootReport); bring-up keeps its
// own facts instead (BSP_dbgBootMark / BSP_dbgBootReport, the panel's and the
// asset's own deferred reports). Pool and queue are file statics: no
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
// Three pre-scheduler boundaries: before BSP_init, after BSP_init, and
// after task setup. Assets/NVS load later on the Flash AO and are excluded.
// Record 79 (three intervals, including old asset loading) is retired.
#define BSP_DBG_BOOT_MARKS 3U
#define BSP_TRACE_BOOT_TIMING 94U

static uint32_t BSP_dbgBootMs_[BSP_DBG_BOOT_MARKS];
static uint8_t BSP_dbgBootMarkIdx_;

void BSP_dbgBootMark(void) {
    if (BSP_dbgBootMarkIdx_ < BSP_DBG_BOOT_MARKS) {
        BSP_dbgBootMs_[BSP_dbgBootMarkIdx_] = HAL_GetTick();
        ++BSP_dbgBootMarkIdx_;
    }
}
//............................................................................
// Record 94: BSP bring-up and task setup, each a big-endian uint16 ms.
// BSP time includes LCD, SRAM and SPI setup; this is not time to first frame.
void BSP_dbgBootReport(void) {
    uint32_t const board = BSP_dbgBootMs_[1] - BSP_dbgBootMs_[0];
    uint32_t const tasks = BSP_dbgBootMs_[2] - BSP_dbgBootMs_[1];

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

    SM_TRACE_BEGIN(BSP_TRACE_BOOT_TIMING, 4)
        SM_TRACE_PAYLOAD_BE((uint8_t)((board >> 8) & 0xFFU))
        SM_TRACE_PAYLOAD_BE((uint8_t)(board & 0xFFU))
        SM_TRACE_PAYLOAD_BE((uint8_t)((tasks >> 8) & 0xFFU))
        SM_TRACE_PAYLOAD_BE((uint8_t)(tasks & 0xFFU))
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
