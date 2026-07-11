#ifndef STM32_COMMON_H
#define STM32_COMMON_H

#include <stdint.h>
#include "dmfmc_port.h"

/**
 * @brief Busy-wait for approximately the given number of microseconds.
 *
 * Calibrated from the AHB clock frequency reported by dmclk_port. Used
 * instead of dmosi_thread_sleep() because SDRAM bring-up runs during
 * dmdevfs's driver configuration pass, before the RTOS scheduler is started
 * (see dmod-boot/src/main.c) - thread_sleep() would not yield the expected
 * delay, or any delay at all, that early in boot.
 *
 * @param us Microseconds to wait
 */
void stm32_fmc_busy_wait_us(uint32_t us);

/**
 * @brief Shared FMC global interrupt handler.
 *
 * Called by each family's DMOD_IRQ_HANDLER(STM32_FMC_IRQn). Reads the
 * SDSR/SDRTR refresh-error flag, clears it, and dispatches to the handler
 * registered via dmfmc_port_add_interrupt_handler(), if any.
 */
void stm32_fmc_irq_handler(void);

#endif // STM32_COMMON_H
