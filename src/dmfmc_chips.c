#include "dmfmc_chips.h"
#include "dmfmc_port.h"
#include <string.h>

/* ---- JEDEC bring-up sequences ---- */

static int initialize_mt48lc4m32b2(dmfmc_sdram_bank_t bank, const dmfmc_sdram_chip_params_t *params,
                                    const dmfmc_sdram_port_result_t *result)
{
    (void)params;
    dmfmc_sdram_command_data_t data = {0};
    int ret;

    /* 1-5. Apply power, hold CKE low, provide a stable clock, then wait at
     * least 100us before any command other than COMMAND INHIBIT/NOP. */
    ret = dmfmc_port_sdram_send_command(bank, dmfmc_sdram_command_enable_clock, NULL, 100U);
    if (ret != 0) return ret;

    dmfmc_port_busy_wait_us(100U);

    ret = dmfmc_port_sdram_send_command(bank, dmfmc_sdram_command_normal, NULL, 100U);
    if (ret != 0) return ret;

    /* 6-7. Precharge all banks, then wait at least tRP. */
    ret = dmfmc_port_sdram_send_command(bank, dmfmc_sdram_command_precharge_all, &data, 100U);
    if (ret != 0) return ret;

    /* 8-11. Two AUTO REFRESH cycles, each followed by at least tRFC. The
     * controller's BUSY flag already enforces tRC between refreshes; the
     * datasheet-mandated settle time on top of that is small enough that
     * relying on the BUSY wait alone is sufficient here. */
    data.number_of_auto_refresh = 8U;
    ret = dmfmc_port_sdram_send_command(bank, dmfmc_sdram_command_auto_refresh, &data, 100U);
    if (ret != 0) return ret;

    ret = dmfmc_port_sdram_send_command(bank, dmfmc_sdram_command_auto_refresh, &data, 100U);
    if (ret != 0) return ret;

    /* 12. Program the mode register: burst length 1, sequential, CAS latency
     * as actually configured by the port layer, standard operating mode,
     * single-location write burst mode. */
    data.mode_register = (uint16_t)(
        (0U << 0) |                              /* burst length = 1 */
        (0U << 3) |                              /* burst type = sequential */
        ((result->cas_latency_cycles & 0x7U) << 4) |
        (0U << 7) |                              /* operating mode = standard */
        (1U << 9));                              /* write burst mode = single location */

    return dmfmc_port_sdram_send_command(bank, dmfmc_sdram_command_load_mode_register, &data, 100U);
}

/* ---- Chip database ---- */

const dmfmc_chip_info_t dmfmc_chip_info_mt48lc4m32b2 = {
    .memory_type = dmfmc_memory_type_sdram,
    .name        = "MT48LC4M32B2",
    .params.sdram = {
        .size_bytes                      = 16U * 1024U * 1024U,  /* 128 Mbit = 4M x 32 */
        .bank_size_bytes                 = 4U * 1024U * 1024U,
        .number_of_banks                 = 4U,
        .data_bus_width                  = dmfmc_data_bus_width_32,
        .cas_latency_mask                = dmfmc_cas_latency_1 | dmfmc_cas_latency_2 | dmfmc_cas_latency_3,
        .cas_latency_ns                  = 20U,
        .auto_precharge_possible         = true,
        .auto_refresh_possible           = true,
        .self_refresh_possible           = true,
        .auto_refresh_period_us          = 64000U,   /* 64ms / 4096 rows, see dmfmc_port_finish_sdram_initialization */
        .maximum_clock_frequency_hz      = 167000000U,
        .number_of_row_address_bits      = 12U,
        .number_of_column_address_bits   = 8U,
        .active_to_read_write_delay_ns   = 20U,  /* tRCD */
        .precharge_delay_ns              = 20U,  /* tRP */
        .write_recovery_delay_ns         = 14U,  /* tWR */
        .refresh_to_activate_delay_ns    = 70U,  /* tRC */
        .min_self_refresh_period_ns      = 70U,  /* tRAS */
        .exit_self_refresh_delay_ns      = 70U,  /* tXSR */
        .cycles_to_delay_after_load_mode = 2U,   /* tMRD */
        .use_burst_read                  = true,
    },
    .init_function = initialize_mt48lc4m32b2,
};

/* ---- Lookup ---- */

static const dmfmc_chip_info_t *const s_known_chips[] = {
    &dmfmc_chip_info_mt48lc4m32b2,
};

/* strcasecmp()/tolower() pull in libc tables (_ctype_) that are not linked
 * into these embedded builds - fold ASCII case by hand instead. */
static char to_lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int names_match_ci(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0')
    {
        if (to_lower_ascii(*a) != to_lower_ascii(*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

const dmfmc_chip_info_t *dmfmc_chips_find(const char *name)
{
    if (name == NULL) return NULL;

    for (size_t i = 0; i < (sizeof(s_known_chips) / sizeof(s_known_chips[0])); i++)
    {
        if (names_match_ci(s_known_chips[i]->name, name))
            return s_known_chips[i];
    }
    return NULL;
}
