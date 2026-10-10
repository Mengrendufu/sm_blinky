/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"
#include "stm32f1xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp.h"   // SM_NORETURN, and the fault stash helpers below
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// Fault exit. An assertion already resets the board (SM_onAssert, with its
// record on the wire); a fault on the bare 'while (1)' below would just
// freeze everything instead, the one outcome the project does not accept
// (AGENTS.md invariant 7). So a fault blinks the LED twice - two pulses are
// the signature, telling the bench "this was a fault, not an assert" - and
// then resets too.
// The fault stash lives in the cross reset area the linker script keeps at
// the top of RAM: the bootloader owns the first word there (bl_main.c), the
// startup code never clears it, and the MSP starts at its base and grows
// down, so nothing overwrites it. A fault writes its frame here and resets;
// the application's main() reports it on the way up (record 66). Without it a
// fault is silent - two LED blinks and a reset, nothing on the wire.
#define IT_FAULT_STASH_WORDS 6U

static volatile uint32_t *IT_faultStash_(void)
{
  extern uint32_t _snoinit;

  return (volatile uint32_t *)(void *)(((uintptr_t)&_snoinit) + 4U);
}
//............................................................................
uint32_t *BSP_dbgFaultStash(void)
{
  return (uint32_t *)(void *)IT_faultStash_();
}

static void Fault_reset_(void);
//............................................................................
// Stacked frame: R0 R1 R2 R3 R12 LR PC xPSR, so the faulting PC is index 6.
// Not static, and marked used: the only reference to it is the "bl" inside
// the naked stub above, which the compiler cannot see - a static one gets
// dropped and the link fails with an undefined reference.
__attribute__((used)) void BSP_faultEntry_(uint32_t const *frame)
{
  volatile uint32_t * const s = IT_faultStash_();

  s[0] = frame[6];
  s[1] = frame[5];
  s[2] = frame[7];
  s[3] = SCB->CFSR;
  s[4] = SCB->HFSR;
  s[5] = SCB->BFAR;

  Fault_reset_();
}
//............................................................................
static void Fault_reset_(void)
{
  volatile uint32_t spin;
  uint8_t i;

  for (i = 0U; i < 2U; ++i) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    for (spin = 0U; spin < 400000U; ++spin) {
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    for (spin = 0U; spin < 400000U; ++spin) {
    }
  }

  NVIC_SystemReset();

  for (;;) {
  }
}
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M3 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
// The entries are naked stubs: the frame the CPU pushed sits at the stack
// pointer as it was at entry, and a plain C handler has already moved it by
// the time it could read it. The stub takes MSP or PSP from EXC_RETURN's
// bit 2 and hands the frame over before any prologue runs.
__attribute__((naked, noreturn)) void HardFault_Handler(void)
{
  __ASM volatile ("tst lr, #4       \n\t"
                  "ite eq           \n\t"
                  "mrseq r0, msp    \n\t"
                  "mrsne r0, psp    \n\t"
                  "bl BSP_faultEntry_\n\t"
                  "b .              \n\t");
}

__attribute__((alias("HardFault_Handler"), noreturn))
void MemManage_Handler(void);
__attribute__((alias("HardFault_Handler"), noreturn))
void BusFault_Handler(void);
__attribute__((alias("HardFault_Handler"), noreturn))
void UsageFault_Handler(void);

/**
  * @brief This function handles Memory management fault.
  */


/**
  * @brief This function handles Prefetch fault, memory access fault.
  */


/**
  * @brief This function handles Undefined instruction or illegal state.
  */


/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F1xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f1xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles TIM1 update interrupt.
  */
void TIM1_UP_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_UP_IRQn 0 */

  /* USER CODE END TIM1_UP_IRQn 0 */
  HAL_TIM_IRQHandler(&htim1);
  /* USER CODE BEGIN TIM1_UP_IRQn 1 */

  /* USER CODE END TIM1_UP_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
