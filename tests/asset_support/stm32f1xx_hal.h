#ifndef TEST_ASSET_HAL_H_
#define TEST_ASSET_HAL_H_
#include <stdint.h>

typedef struct {
    uint32_t BaudRate, WordLength, StopBits, Parity, Mode;
    uint32_t HwFlowCtl, OverSampling;
} TestUartInit;
typedef struct __UART_HandleTypeDef {
    void *Instance; TestUartInit Init;
} UART_HandleTypeDef;
typedef struct { void *Instance; } TIM_HandleTypeDef;
typedef struct {
    uint32_t PLLState, PLLSource, PLLMUL;
} TestPLL;
typedef struct {
    uint32_t OscillatorType, HSEState, HSEPredivValue, HSIState;
    TestPLL PLL;
} RCC_OscInitTypeDef;
typedef struct {
    uint32_t ClockType, SYSCLKSource, AHBCLKDivider;
    uint32_t APB1CLKDivider, APB2CLKDivider;
} RCC_ClkInitTypeDef;
typedef struct { uint32_t ICER[8], ICPR[8]; } TestNVIC;
typedef struct { uint32_t VTOR; } TestSCB;
extern TestNVIC *NVIC;
extern TestSCB *SCB;
#define HAL_OK 0
#define TIM1 ((void *)1)
#define USART1 ((void *)2)
#define UART_WORDLENGTH_8B 0
#define UART_STOPBITS_1 0
#define UART_PARITY_NONE 0
#define UART_MODE_TX_RX 0
#define UART_HWCONTROL_NONE 0
#define UART_OVERSAMPLING_16 0
#define RCC_OSCILLATORTYPE_HSE 0
#define RCC_HSE_ON 0
#define RCC_HSE_PREDIV_DIV1 0
#define RCC_HSI_ON 0
#define RCC_PLL_ON 0
#define RCC_PLLSOURCE_HSE 0
#define RCC_PLL_MUL9 0
#define RCC_CLOCKTYPE_HCLK 0
#define RCC_CLOCKTYPE_SYSCLK 0
#define RCC_CLOCKTYPE_PCLK1 0
#define RCC_CLOCKTYPE_PCLK2 0
#define RCC_SYSCLKSOURCE_PLLCLK 0
#define RCC_SYSCLK_DIV1 0
#define RCC_HCLK_DIV2 0
#define RCC_HCLK_DIV1 0
#define FLASH_LATENCY_2 0
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
#define __set_MSP(x) ((void)(x))
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __get_IPSR() 0U
void HAL_Init(void);
void HAL_IncTick(void);
void HAL_DeInit(void);
void HAL_Delay(uint32_t ms);
uint32_t HAL_GetTick(void);
int HAL_RCC_OscConfig(RCC_OscInitTypeDef *config);
int HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *config, uint32_t latency);
int HAL_UART_Init(UART_HandleTypeDef *uart);
int HAL_UART_DeInit(UART_HandleTypeDef *uart);
int HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                      uint16_t len, uint32_t timeout);
int HAL_UART_Receive(UART_HandleTypeDef *uart, uint8_t *data,
                     uint16_t len, uint32_t timeout);
_Noreturn void NVIC_SystemReset(void);
void Error_Handler(void);
#endif
