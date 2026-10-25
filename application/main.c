#include "stm32f1xx_hal.h"
#include "sst.h"
#include "bsp.h"
#include "application.h"
#include "ui.h"
#include "FreeRTOS.h"
#include "task.h"

UART_HandleTypeDef huart1;

void SystemClock_Config(void);
void Error_Handler(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);

int main(void) {
    // The app image lives at 0x08004000; the first 16 KB belong to the
    // bootloader. On a real reset the hardware loads MSP and PC from the
    // vector table at 0x08000000 - the bootloader's - so point VTOR at ours
    // before anything can take an interrupt. The bootloader sets the same
    // value just before it jumps; this line covers the other case, where
    // J-Link loads the app image on its own and no bootloader ran.
    SCB->VTOR = 0x08004000U;

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    BSP_dbgBootMark();   // 0: the board is up, nothing brought up yet

    // BSP_init() brings up the panel - which waits out the sleep-out time
    // with HAL_Delay(), so it needs interrupts - then the SRAM and the SPI
    // flash. Nothing here touches the trace channel: a push before the
    // scheduler enters a FreeRTOS critical section whose nesting counter
    // still holds its sentinel, and interrupts stay masked from there until
    // the scheduler takes over (that used to freeze HAL_GetTick() mid
    // bring-up, which would hang the panel's own retry delay). The boot facts
    // are kept and reported once a task runs - see Blinky_init.
    BSP_init();

    BSP_dbgBootMark();   // 1: panel out of sleep, SRAM and SPI flash ready
    BSP_dbgBootMark();   // 2: retired pre-scheduler asset interval


    // Flash AO loads assets after scheduling; UI waits on readiness events.
    Application_tickStart();

    // Bind before post: UI inbox and Flash AO queue must exist before any
    // AO submits work. SST_Task_start initializes synchronously before
    // returning; Flash TOP only posts its first initialization event.
    Ui_start();
    SST_init();
    SST_start(); // Initialize event pools before AO entry actions allocate.
    AO_start();

    BSP_dbgBootMark();   // 3: everything is created, the scheduler is next

    vTaskStartScheduler();

    while (1) {
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = BSP_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_LED_PORT, &GPIO_InitStruct);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        HAL_IncTick();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
