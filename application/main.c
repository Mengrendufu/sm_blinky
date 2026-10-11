#include "stm32f1xx_hal.h"
#include "sst.h"
#include "asset.h"
#include "bsp.h"
#include "bsp_lcd.h"   // the boot splash goes straight to the panel
#include "application.h"
#include "ui.h"
#include "flash.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

UART_HandleTypeDef huart1;
static TimerHandle_t SST_sysTimer;
static StaticTimer_t SST_sysTimer_sto;

void SystemClock_Config(void);
void Error_Handler(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void SST_sysTimer_cb(TimerHandle_t xTimer);

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


    // The asset the bootloader wrote, if it got that far. Read here on
    // purpose:
    // the NOR and the external SRAM are both up after BSP_init(), and this is
    // still before the first FreeRTOS call. The UI thread picks the result up
    // when it builds its screen.
    (void)Asset_load();

    // The boot splash: one whole frame pushed straight from the external SRAM
    // to the panel, no LVGL and no buffer copy in the way. Slot 2 is the one
    // that reads as a photograph. The UI thread holds its first frame for a
    // moment (UI_SPLASH_MS) so this is actually seen.
    {
        Asset const * const asset = Asset_get();

        if (asset->present && (asset->slots >= 3U)) {
            (void)BSP_lcdShowImage(Asset_slot(asset, 2U),
                                   ASSET_SLOT_BYTES / 2U);
        }
    }

    BSP_dbgBootMark();   // 2: the asset is in the external SRAM, CRC checked

    SST_sysTimer = xTimerCreateStatic(
        "sstTick",
        configTICK_RATE_HZ / BSP_TICKS_PER_SEC,
        pdTRUE,
        (void *)0,
        SST_sysTimer_cb,
        &SST_sysTimer_sto);
    configASSERT(SST_sysTimer != NULL);
    configASSERT(xTimerStart(SST_sysTimer, 0U) == pdPASS);

    // Bind before post: Ui_start()/Flash_start() build their task and point
    // the channel's wake slot at it; AO_start() is what posts into those
    // channels, from the initial transition of its first AO. A post into an
    // unbound channel is accepted silently with the wake dropped - a flash
    // caller would then park on a completion that can never arrive - so this
    // order is the contract, not the timing luck that used to cover it.
    SST_init();
    Ui_start();
    Flash_start();
    AO_start();
    SST_start();

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

static void SST_sysTimer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    SST_TimeEvt_tick();
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
