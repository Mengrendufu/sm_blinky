#ifndef TEST_FLASH_HAL_H_
#define TEST_FLASH_HAL_H_
#include <stdint.h>
#include <stdbool.h>

typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct __UART_HandleTypeDef { int unused; } UART_HandleTypeDef;
typedef struct {
    uint32_t TypeErase, PageAddress, NbPages;
} FLASH_EraseInitTypeDef;
#define FLASH_PAGE_SIZE 0x800U
#define FLASH_TYPEERASE_PAGES 0U
#define FLASH_TYPEPROGRAM_HALFWORD 1U
#define FLASH_FLAG_BSY 1U
#define FLASH_FLAG_EOP 2U
#define FLASH_FLAG_PGERR 4U
#define FLASH_FLAG_WRPERR 8U
extern uint32_t Test_flashFlags;
#define __HAL_FLASH_GET_FLAG(flag) (Test_flashFlags & (flag))
#define __HAL_FLASH_CLEAR_FLAG(flag) (Test_flashFlags &= ~(flag))
#define __DSB() ((void)0)
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t addr,
                                   uint64_t value);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *request,
                                   uint32_t *error);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart,
                                   uint8_t *data, uint16_t size,
                                   uint32_t timeout);
#endif
