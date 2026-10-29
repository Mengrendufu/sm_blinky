#include "stm32f1xx_hal.h"
#include "bl_link.h"
#include "asset_link.h"
#include "hdlc_frame.h"

bool Bl_linkSend(UART_HandleTypeDef *uart, uint8_t id,
                 uint8_t const *payload, uint8_t len) {
    static uint8_t sequence;
    static uint8_t wire[2U * (UINT8_MAX + 4U) + 2U];
    size_t const used = HdlcFrame_encode(sequence++, id, payload, len,
                                        wire, sizeof(wire));
    return used != 0U
           && HAL_UART_Transmit(uart, wire, (uint16_t)used, 100U) == HAL_OK;
}
