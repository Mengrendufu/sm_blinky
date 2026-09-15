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
// #include "stm32f103xe.h"

//============================================================================
void BSP_init(void) {
    //........................................................................
    // DWT enable...
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    SystemCoreClockUpdate(); // Update SystemCoreClock
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
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, 0);
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
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, 1);
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
void SST_onIdle(void) {
    // __WFI();
    for (;;) {
        SM_Tracer_idle(&SM_Tracer_inst);
    }
}
