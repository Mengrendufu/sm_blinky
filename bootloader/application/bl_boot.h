#ifndef BL_BOOT_H_
#define BL_BOOT_H_

struct __UART_HandleTypeDef;

// Run the single boot-stage HSM after platform/UART initialization.
// HAL tick and interrupts must be live. Owns UART access until App handover
// (which deinitializes UART); otherwise remains in the boot event loop.
void Bl_Boot_run(struct __UART_HandleTypeDef *uart)
    __attribute__((noreturn));

#endif // BL_BOOT_H_
