#ifndef DMFMC_STM32F7_REGS_H
#define DMFMC_STM32F7_REGS_H

#include <stdint.h>

/* ======================================================================
 *               STM32F7 Specific FMC Definitions
 *
 *   The FMC SDRAM controller itself is identical to STM32F4's (see
 *   stm32_common_regs.h) - this file is reserved for values that do turn
 *   out to differ once more STM32F7 parts are validated (e.g. a part-
 *   specific HSI/clock default).
 * ====================================================================== */

/* Asynchronous NOR/SRAM bank 1, separate from the SDRAM banks. */
#define STM32_FMC_NORSRAM_BCR1      (*(volatile uint32_t *)0xA0000000UL)
#define FMC_BCR1_MBKEN              (1U << 0)

#endif // DMFMC_STM32F7_REGS_H
