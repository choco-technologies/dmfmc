#ifndef DMFMC_STM32F4_REGS_H
#define DMFMC_STM32F4_REGS_H

#include <stdint.h>

/* ======================================================================
 *               STM32F4 Specific FMC Definitions
 *
 *   Only STM32F42x/43x/469/479 parts have an FMC with SDRAM support (smaller
 *   F4 parts only have the FSMC, which cannot drive SDRAM); this port targets
 *   that subset of the F4 family. The FMC SDRAM controller itself is
 *   identical to STM32F7's (see stm32_common_regs.h) - this file is
 *   reserved for values that do turn out to differ once validated on real
 *   hardware.
 * ====================================================================== */

#endif // DMFMC_STM32F4_REGS_H
