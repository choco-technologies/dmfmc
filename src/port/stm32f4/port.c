#define DMOD_ENABLE_REGISTRATION    ON
#include "dmfmc_port.h"
#include "dmod.h"
#include "dmclk_port.h"
#include "../stm32_common/stm32_common.h"
#include "port/stm32_common_regs.h"
#include "port/stm32f4_regs.h"

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    Dmod_Printf("DMFMC port module initialized (STM32F4)\n");
    return 0;
}

int dmod_deinit(void)
{
    Dmod_Printf("DMFMC port module deinitialized (STM32F4)\n");
    return 0;
}

/* ---- ISR handler ----
 *
 * All register-level logic lives in stm32_common.c, shared with STM32F7
 * (identical FMC SDRAM controller IP block on the F42x/43x/469/479 parts
 * that have an FMC); only the NVIC IRQ number is declared per family, via
 * DMOD_IRQ_HANDLER below. */

DMOD_IRQ_HANDLER(STM32_FMC_IRQn)
{
    stm32_fmc_irq_handler();
}
