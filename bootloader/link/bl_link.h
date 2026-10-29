#ifndef BL_LINK_H_
#define BL_LINK_H_
#include <stdbool.h>
#include <stdint.h>
struct __UART_HandleTypeDef;

// Boot-main-only blocking transmitter; shared by replies and all logs.
bool Bl_linkSend(struct __UART_HandleTypeDef *uart, uint8_t id,
                 uint8_t const *payload, uint8_t len);
#endif
