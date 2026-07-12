#include "stm32_common.h"
#include "dmod.h"
#include "dmclk_port.h"
#include "port/stm32_common_regs.h"
#include <errno.h>
#include <stddef.h>

/* ---- Per-bank software state ---- */

typedef struct
{
    bool                    configured;
    uint32_t                size_bytes;
    uint32_t                sdclk_hz;
    dmfmc_data_bus_width_t  data_bus_width;
} bank_state_t;

static bank_state_t s_bank_state[2]; /* index 0 = bank1, index 1 = bank2 */

static dmfmc_port_interrupt_handler_t s_irq_handler  = NULL;
static void                          *s_irq_user_ptr = NULL;

/* ---- Helpers ---- */

static FMC_Bank5_6_TypeDef *fmc(void)
{
    return (FMC_Bank5_6_TypeDef *)STM32_FMC_BANK5_6_BASE;
}

static int bank_index(dmfmc_sdram_bank_t bank, uint32_t *out_idx)
{
    if (bank != dmfmc_sdram_bank_1 && bank != dmfmc_sdram_bank_2)
        return -EINVAL;
    *out_idx = (uint32_t)bank - 1U;
    return 0;
}

/* HCLK (AHB) prescaler (RCC_CFGR HPRE field, bits[7:4]) division factor.
 * Encoding is identical on STM32F4 and STM32F7: MSB=0 -> not divided;
 * 1000=/2, 1001=/4, 1010=/8, 1011=/16, 1100=/64, 1101=/128, 1110=/256, 1111=/512. */
static uint32_t hpre_prescaler_div(uint32_t hpre_bits)
{
    static const uint16_t table[8] = {2U, 4U, 8U, 16U, 64U, 128U, 256U, 512U};
    if ((hpre_bits & 0x8U) == 0U) return 1U;
    return table[hpre_bits & 0x7U];
}

static uint32_t get_hclk_frequency(void)
{
    volatile FMC_RCC_TypeDef *RCC = (FMC_RCC_TypeDef *)STM32_FMC_RCC_BASE;

    uint32_t sysclk = (uint32_t)dmclk_port_get_current_frequency();
    if (sysclk == 0U)
        sysclk = 16000000U; /* HSI fallback, should not normally happen */

    uint32_t hpre = (RCC->CFGR >> 4) & 0xFU;
    return sysclk / hpre_prescaler_div(hpre);
}

void stm32_fmc_busy_wait_us(uint32_t us)
{
    uint32_t hclk = get_hclk_frequency();
    uint32_t cycles_per_us = hclk / 1000000U;
    if (cycles_per_us == 0U) cycles_per_us = 1U;

    /* Conservative estimate of loop iterations per microsecond (errs long,
     * which is the safe direction for SDRAM JEDEC timing margins). */
    volatile uint32_t count = (cycles_per_us / 4U) * us;
    if (count == 0U) count = 1U;

    while (count--)
    {
        __asm volatile ("nop");
    }
}

/* Rounds a delay in nanoseconds up to a whole number of SDCLK cycles,
 * clamped to the [1,16] range the FMC_SDTRx fields can encode. */
static uint32_t ns_to_cycles(uint32_t ns, uint32_t sdclk_hz)
{
    if (ns == 0U || sdclk_hz == 0U) return 1U;

    uint64_t cycles = ((uint64_t)ns * (uint64_t)sdclk_hz + 999999999ULL) / 1000000000ULL;
    if (cycles < 1U)  cycles = 1U;
    if (cycles > 16U) cycles = 16U;
    return (uint32_t)cycles;
}

/* Picks the fastest SDCLK = HCLK/{2,3} that does not exceed the chip's
 * maximum rated clock frequency. */
static int pick_sdclk_divider(uint32_t hclk_hz, uint32_t max_chip_hz, uint32_t *out_divider, uint32_t *out_sdclk_hz)
{
    static const uint32_t dividers[] = {2U, 3U};

    for (size_t i = 0; i < (sizeof(dividers) / sizeof(dividers[0])); i++)
    {
        uint32_t sdclk = hclk_hz / dividers[i];
        if (sdclk > 0U && sdclk <= max_chip_hz)
        {
            *out_divider = dividers[i];
            *out_sdclk_hz = sdclk;
            return 0;
        }
    }
    return -EINVAL;
}

static uint32_t encode_column_bits(uint8_t bits)
{
    /* NC field: 0=8 bits .. 3=11 bits */
    if (bits < 8U) bits = 8U;
    if (bits > 11U) bits = 11U;
    return (uint32_t)(bits - 8U) & 0x3U;
}

static uint32_t encode_row_bits(uint8_t bits)
{
    /* NR field: 0=11 bits .. 2=13 bits */
    if (bits < 11U) bits = 11U;
    if (bits > 13U) bits = 13U;
    return (uint32_t)(bits - 11U) & 0x3U;
}

static uint32_t encode_data_width(dmfmc_data_bus_width_t width)
{
    switch (width)
    {
        case dmfmc_data_bus_width_8:  return 0U;
        case dmfmc_data_bus_width_16: return 1U;
        default:                      return 2U; /* dmfmc_data_bus_width_32 */
    }
}

/* Picks the slowest CAS latency (in SDCLK cycles) that both satisfies the
 * chip's minimum access time at the configured SDCLK, and is one the chip
 * actually supports (CasLatencyMask). Falls back to the fastest supported
 * latency if the chip's timing would need more than 3 cycles. */
static uint32_t pick_cas_latency_cycles(const dmfmc_sdram_chip_params_t *chip, uint32_t sdclk_hz)
{
    uint32_t cycles = ns_to_cycles(chip->cas_latency_ns, sdclk_hz);
    if (cycles > 3U) cycles = 3U;
    if (cycles < 1U) cycles = 1U;

    for (uint32_t c = cycles; c <= 3U; c++)
    {
        if (chip->cas_latency_mask & (1U << (c - 1U)))
            return c;
    }
    for (uint32_t c = 1U; c <= 3U; c++)
    {
        if (chip->cas_latency_mask & (1U << (c - 1U)))
            return c;
    }
    return 2U;
}

/* ---- Port API implementation ----
 *
 * dmod_init()/dmod_deinit() (the module-level lifecycle hooks) live in each
 * family's port.c, not here - a module may only define them once, and this
 * file is compiled into the same dmfmc_port module as port.c. s_bank_state
 * starts zeroed regardless (static storage duration), so there is nothing
 * to reset here. */

dmod_dmfmc_port_api_declaration(1.0, int, _init, ( void ))
{
    volatile FMC_RCC_TypeDef *RCC = (FMC_RCC_TypeDef *)STM32_FMC_RCC_BASE;
    RCC->AHB3ENR |= RCC_AHB3ENR_FMCEN;
    (void)RCC->AHB3ENR; /* barrier: ensure the clock is on before the FMC is touched */
    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, int, _deinit, ( void ))
{
    for (int i = 0; i < 2; i++)
        s_bank_state[i].configured = false;
    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, int, _configure_sdram,
    ( dmfmc_sdram_bank_t bank, dmfmc_data_bus_width_t data_bus_width,
      const dmfmc_sdram_chip_params_t *chip, dmfmc_sdram_port_result_t *out_result ))
{
    uint32_t idx;
    if (chip == NULL || out_result == NULL || bank_index(bank, &idx) != 0)
        return -EINVAL;

    dmfmc_data_bus_width_t width = (data_bus_width == dmfmc_data_bus_width_default) ? chip->data_bus_width : data_bus_width;

    uint32_t hclk = get_hclk_frequency();
    uint32_t divider, sdclk_hz;
    if (pick_sdclk_divider(hclk, chip->maximum_clock_frequency_hz, &divider, &sdclk_hz) != 0)
    {
        DMOD_LOG_ERROR("FMC: cannot derive an SDCLK <= %u Hz from HCLK=%u Hz\n",
            (unsigned)chip->maximum_clock_frequency_hz, (unsigned)hclk);
        return -EINVAL;
    }

    uint32_t cas_cycles = pick_cas_latency_cycles(chip, sdclk_hz);

    FMC_Bank5_6_TypeDef *FMC = fmc();

    uint32_t sdcr = 0U;
    sdcr |= (encode_column_bits(chip->number_of_column_address_bits) << FMC_SDCR_NC_Pos);
    sdcr |= (encode_row_bits(chip->number_of_row_address_bits) << FMC_SDCR_NR_Pos);
    sdcr |= (encode_data_width(width) << FMC_SDCR_MWID_Pos);
    if (chip->number_of_banks > 2U) sdcr |= FMC_SDCR_NB;
    sdcr |= ((cas_cycles & 0x3U) << FMC_SDCR_CAS_Pos);

    /* SDCLK divider and burst-read are shared fields that only take effect
     * from SDCR1, regardless of which bank they are written through. */
    uint32_t shared_cr = ((divider & 0x3U) << FMC_SDCR_SDCLK_Pos) | (chip->use_burst_read ? FMC_SDCR_RBURST : 0U);

    if (bank == dmfmc_sdram_bank_1)
    {
        FMC->SDCR[0] = sdcr | shared_cr;
    }
    else
    {
        FMC->SDCR[1] = sdcr;
        FMC->SDCR[0] = (FMC->SDCR[0] & ~(FMC_SDCR_SDCLK_Msk | FMC_SDCR_RBURST | FMC_SDCR_RPIPE_Msk)) | shared_cr;
    }

    uint32_t trcd = ns_to_cycles(chip->active_to_read_write_delay_ns, sdclk_hz);
    uint32_t twr  = ns_to_cycles(chip->write_recovery_delay_ns, sdclk_hz);
    uint32_t tras = ns_to_cycles(chip->min_self_refresh_period_ns, sdclk_hz);
    uint32_t txsr = ns_to_cycles(chip->exit_self_refresh_delay_ns, sdclk_hz);
    uint32_t tmrd = chip->cycles_to_delay_after_load_mode;
    if (tmrd < 1U) tmrd = 1U;
    if (tmrd > 16U) tmrd = 16U;

    uint32_t sdtr = 0U;
    sdtr |= (((trcd - 1U) & 0xFU) << FMC_SDTR_TRCD_Pos);
    sdtr |= (((twr  - 1U) & 0xFU) << FMC_SDTR_TWR_Pos);
    sdtr |= (((tras - 1U) & 0xFU) << FMC_SDTR_TRAS_Pos);
    sdtr |= (((txsr - 1U) & 0xFU) << FMC_SDTR_TXSR_Pos);
    sdtr |= (((tmrd - 1U) & 0xFU) << FMC_SDTR_TMRD_Pos);

    /* TRC and TRP are shared fields that only take effect from SDTR1. */
    uint32_t trc = ns_to_cycles(chip->refresh_to_activate_delay_ns, sdclk_hz);
    uint32_t trp = ns_to_cycles(chip->precharge_delay_ns, sdclk_hz);
    uint32_t shared_tr = (((trc - 1U) & 0xFU) << FMC_SDTR_TRC_Pos) | (((trp - 1U) & 0xFU) << FMC_SDTR_TRP_Pos);

    if (bank == dmfmc_sdram_bank_1)
    {
        FMC->SDTR[0] = sdtr | shared_tr;
    }
    else
    {
        FMC->SDTR[1] = sdtr;
        FMC->SDTR[0] = (FMC->SDTR[0] & ~(FMC_SDTR_TRC_Msk | FMC_SDTR_TRP_Msk)) | shared_tr;
    }

    out_result->configured_frequency_hz = sdclk_hz;
    out_result->cas_latency_cycles = cas_cycles;
    out_result->memory_start = (void *)(uintptr_t)((bank == dmfmc_sdram_bank_1) ? STM32_FMC_SDRAM_BANK1_BASE : STM32_FMC_SDRAM_BANK2_BASE);
    out_result->memory_size_bytes = chip->size_bytes;
    out_result->data_bus_width = width;

    s_bank_state[idx].configured    = true;
    s_bank_state[idx].size_bytes    = chip->size_bytes;
    s_bank_state[idx].sdclk_hz      = sdclk_hz;
    s_bank_state[idx].data_bus_width = width;

    DMOD_LOG_INFO("FMC: SDRAM bank %u configured at %u Hz (CAS=%u, width=%u bytes)\n",
        (unsigned)bank, (unsigned)sdclk_hz, (unsigned)cas_cycles, (unsigned)width);

    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, int, _unconfigure_sdram, ( dmfmc_sdram_bank_t bank ))
{
    uint32_t idx;
    if (bank_index(bank, &idx) != 0)
        return -EINVAL;

    s_bank_state[idx].configured = false;
    s_bank_state[idx].size_bytes = 0U;
    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, int, _sdram_send_command,
    ( dmfmc_sdram_bank_t bank, dmfmc_sdram_command_t command,
      const dmfmc_sdram_command_data_t *data, uint32_t timeout_ms ))
{
    uint32_t idx;
    if (bank_index(bank, &idx) != 0)
        return -EINVAL;

    uint32_t mode;
    switch (command)
    {
        case dmfmc_sdram_command_normal:            mode = FMC_SDCMR_MODE_NORMAL;      break;
        case dmfmc_sdram_command_enable_clock:      mode = FMC_SDCMR_MODE_CLK_ENABLE;  break;
        case dmfmc_sdram_command_precharge_all:     mode = FMC_SDCMR_MODE_PALL;        break;
        case dmfmc_sdram_command_auto_refresh:      mode = FMC_SDCMR_MODE_AUTOREFRESH; break;
        case dmfmc_sdram_command_load_mode_register:mode = FMC_SDCMR_MODE_LOADMODEREG; break;
        case dmfmc_sdram_command_self_refresh:      mode = FMC_SDCMR_MODE_SELFREFRESH; break;
        case dmfmc_sdram_command_power_down:        mode = FMC_SDCMR_MODE_POWERDOWN;   break;
        default: return -EINVAL;
    }

    uint32_t sdcmr = mode | ((bank == dmfmc_sdram_bank_1) ? FMC_SDCMR_CTB1 : FMC_SDCMR_CTB2);

    if (command == dmfmc_sdram_command_auto_refresh && data != NULL)
    {
        uint32_t n = data->number_of_auto_refresh;
        if (n < 1U)  n = 1U;
        if (n > 16U) n = 16U;
        sdcmr |= (((n - 1U) << FMC_SDCMR_NRFS_Pos) & FMC_SDCMR_NRFS_Msk);
    }

    if (command == dmfmc_sdram_command_load_mode_register && data != NULL)
    {
        sdcmr |= (((uint32_t)data->mode_register << FMC_SDCMR_MRD_Pos) & FMC_SDCMR_MRD_Msk);
    }

    FMC_Bank5_6_TypeDef *FMC = fmc();

    /* Loop-count timeout (not calibrated to real time): the controller only
     * ever stays BUSY for a handful of SDCLK cycles per command, so this is
     * purely a safety net against a hung/misconfigured controller. */
    uint32_t spin = timeout_ms * 1000U;
    while (FMC->SDSR & FMC_SDSR_BUSY)
    {
        if (spin-- == 0U) return -ETIMEDOUT;
    }

    FMC->SDCMR = sdcmr;

    spin = timeout_ms * 1000U;
    while (FMC->SDSR & FMC_SDSR_BUSY)
    {
        if (spin-- == 0U) return -ETIMEDOUT;
    }

    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, int, _finish_sdram_initialization,
    ( dmfmc_sdram_bank_t bank, const dmfmc_sdram_chip_params_t *chip ))
{
    uint32_t idx;
    if (chip == NULL || bank_index(bank, &idx) != 0 || !s_bank_state[idx].configured)
        return -EINVAL;

    /* COUNT = (refresh period / number of rows) * SDCLK - 20, per ST's
     * application-note formula for FMC_SDRTR (the -20 cycle margin accounts
     * for worst-case interrupt/refresh-request latency).
     *
     * When the configured data bus is narrower than the chip's native width
     * (e.g. a 32-bit chip wired through only 16 data lines), divide the row
     * count by that ratio, matching a previously verified driver for this
     * exact scenario - without this, refresh runs too infrequently and the
     * SDRAM's contents decay over time even though initial configuration and
     * the JEDEC bring-up sequence both appear to succeed. */
    uint32_t rows = 1U << chip->number_of_row_address_bits;
    uint32_t width_ratio = (uint32_t)chip->data_bus_width / (uint32_t)s_bank_state[idx].data_bus_width;
    if (width_ratio > 1U)
        rows /= width_ratio;

    uint64_t count = ((uint64_t)chip->auto_refresh_period_us * (uint64_t)s_bank_state[idx].sdclk_hz)
                     / ((uint64_t)rows * 1000000ULL);
    count = (count > 20U) ? (count - 20U) : 1U;
    if (count > 0x1FFFU) count = 0x1FFFU;

    FMC_Bank5_6_TypeDef *FMC = fmc();
    FMC->SDRTR = (FMC->SDRTR & ~FMC_SDRTR_COUNT_Msk) | (((uint32_t)count << FMC_SDRTR_COUNT_Pos) & FMC_SDRTR_COUNT_Msk);

    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, int, _configure_nor, ( dmfmc_sdram_bank_t bank, dmfmc_data_bus_width_t data_bus_width ))
{
    (void)bank; (void)data_bus_width;
    return -ENOSYS;
}

dmod_dmfmc_port_api_declaration(1.0, int, _configure_nand, ( dmfmc_sdram_bank_t bank, dmfmc_data_bus_width_t data_bus_width ))
{
    (void)bank; (void)data_bus_width;
    return -ENOSYS;
}

dmod_dmfmc_port_api_declaration(1.0, int, _configure_psram, ( dmfmc_sdram_bank_t bank, dmfmc_data_bus_width_t data_bus_width ))
{
    (void)bank; (void)data_bus_width;
    return -ENOSYS;
}

dmod_dmfmc_port_api_declaration(1.0, int, _get_memory_region,
    ( dmfmc_sdram_bank_t bank, void **out_address, uint32_t *out_size ))
{
    uint32_t idx;
    if (bank_index(bank, &idx) != 0 || !s_bank_state[idx].configured)
        return -EINVAL;

    if (out_address != NULL)
        *out_address = (void *)(uintptr_t)((bank == dmfmc_sdram_bank_1) ? STM32_FMC_SDRAM_BANK1_BASE : STM32_FMC_SDRAM_BANK2_BASE);
    if (out_size != NULL)
        *out_size = s_bank_state[idx].size_bytes;

    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, int, _add_interrupt_handler,
    ( dmfmc_port_interrupt_handler_t handler, void *user_ptr ))
{
    if (handler == NULL) return -EINVAL;

    s_irq_handler  = handler;
    s_irq_user_ptr = user_ptr;

    FMC_Bank5_6_TypeDef *FMC = fmc();
    FMC->SDRTR |= FMC_SDRTR_REIE;
    NVIC_ISER[STM32_FMC_IRQn >> 5U] = 1U << (STM32_FMC_IRQn & 0x1FU);
    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, int, _remove_interrupt_handler, ( void *user_ptr ))
{
    if (s_irq_user_ptr != user_ptr) return 0;

    FMC_Bank5_6_TypeDef *FMC = fmc();
    FMC->SDRTR &= ~FMC_SDRTR_REIE;
    NVIC_ICER[STM32_FMC_IRQn >> 5U] = 1U << (STM32_FMC_IRQn & 0x1FU);

    s_irq_handler  = NULL;
    s_irq_user_ptr = NULL;
    return 0;
}

dmod_dmfmc_port_api_declaration(1.0, void, _busy_wait_us, ( uint32_t us ))
{
    stm32_fmc_busy_wait_us(us);
}

/* ---- ISR handler ---- */

void stm32_fmc_irq_handler(void)
{
    FMC_Bank5_6_TypeDef *FMC = fmc();

    if (FMC->SDSR & FMC_SDSR_RE)
    {
        FMC->SDRTR |= FMC_SDRTR_CRE; /* write 1 to clear the refresh error flag */

        if (s_irq_handler != NULL)
        {
            for (uint32_t i = 0; i < 2U; i++)
            {
                if (s_bank_state[i].configured)
                    s_irq_handler(s_irq_user_ptr, (dmfmc_sdram_bank_t)(i + 1U), dmfmc_interrupt_event_refresh_error);
            }
        }
    }
}
