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
// Trace channel. It has to be alive before anything reports, and the
// bring-up below reports through it, so it is the first thing BSP_init()
// does. The pool and the queue are file statics here: no allocation.
static void BSP_Tracer_init(void) {
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
void BSP_init(void) {
    // The tracer first: a record pushed before it exists is dropped without a
    // word, and the panel bring-up below reports.
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
static void BSP_traceStack_(uint8_t recId, uint16_t words) {
    SM_TRACE_BEGIN(recId, 2)
        SM_TRACE_PAYLOAD_BE(words >> 8)
        SM_TRACE_PAYLOAD_BE(words)
    SM_TRACE_END()
}
//............................................................................
void BSP_memReport(uint8_t selfRecId) {
    TaskHandle_t const idle = xTaskGetIdleTaskHandle();
    TaskHandle_t const timer = xTimerGetTimerDaemonTaskHandle();

    BSP_traceStack_(selfRecId,
                    (uint16_t)uxTaskGetStackHighWaterMark((TaskHandle_t)0));
    BSP_traceStack_(BSP_TRACE_STACK_IDLE,
                    (uint16_t)uxTaskGetStackHighWaterMark(idle));
    BSP_traceStack_(BSP_TRACE_STACK_TIMER,
                    (uint16_t)uxTaskGetStackHighWaterMark(timer));
}
//............................................................................
void BSP_traceRaw(char const *text) {
    // Straight to the peripheral, not through HAL: the tracer's writer and
    // this one are different tasks, and HAL's handle would answer HAL_BUSY
    // while the other is mid byte. Register writes cannot be busy, and a few
    // interleaved bytes are fine when the question is which line got out.
    while (*text != '\0') {
        while ((USART1->SR & USART_SR_TXE) == 0U) {
        }
        USART1->DR = (uint16_t)(*text++);
    }
}
//............................................................................
void BSP_ledOn(void) {
    // FreeRTOS tick: the AO beat itself is driven by it, so it cannot freeze.
    uint32_t timeStamp = (uint32_t)xTaskGetTickCount();
    SM_TRACE_BEGIN(11, 4)
        SM_TRACE_PAYLOAD_BE(timeStamp >> 24)
        SM_TRACE_PAYLOAD_BE(timeStamp >> 16)
        SM_TRACE_PAYLOAD_BE(timeStamp >>  8)
        SM_TRACE_PAYLOAD_BE(timeStamp >>  0)
    SM_TRACE_END()
    HAL_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, 0);
}
void BSP_ledOff(void) {
    // FreeRTOS tick: the AO beat itself is driven by it, so it cannot freeze.
    uint32_t timeStamp = (uint32_t)xTaskGetTickCount();
    SM_TRACE_BEGIN(10, 4)
        SM_TRACE_PAYLOAD_BE(timeStamp >> 24)
        SM_TRACE_PAYLOAD_BE(timeStamp >> 16)
        SM_TRACE_PAYLOAD_BE(timeStamp >>  8)
        SM_TRACE_PAYLOAD_BE(timeStamp >>  0)
    SM_TRACE_END()
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
