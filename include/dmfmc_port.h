#ifndef DMFMC_PORT_H
#define DMFMC_PORT_H

#include "dmod_types.h"
#include "dmfmc_port_defs.h"
#include "dmfmc_types.h"

/* --- Lifecycle --- */

dmod_dmfmc_port_api(1.0, int, _init,   ( void ) );
dmod_dmfmc_port_api(1.0, int, _deinit, ( void ) );

/* --- SDRAM configuration --- */

dmod_dmfmc_port_api(1.0, int, _configure_sdram,
    ( dmfmc_sdram_bank_t bank, dmfmc_data_bus_width_t data_bus_width,
      const dmfmc_sdram_chip_params_t *chip, dmfmc_sdram_port_result_t *out_result ) );

dmod_dmfmc_port_api(1.0, int, _unconfigure_sdram, ( dmfmc_sdram_bank_t bank ) );

/**
 * Sends a single JEDEC-level command to the SDRAM controller and waits (up to
 * timeout_ms) for the controller to report it is no longer busy. Used by a
 * chip's bring-up sequence (see dmfmc_chips_run_init_sequence()).
 */
dmod_dmfmc_port_api(1.0, int, _sdram_send_command,
    ( dmfmc_sdram_bank_t bank, dmfmc_sdram_command_t command,
      const dmfmc_sdram_command_data_t *data, uint32_t timeout_ms ) );

/**
 * Programs the auto-refresh timer once the chip's JEDEC bring-up sequence has
 * completed. Must be called last, after dmfmc_chips_run_init_sequence() returns.
 */
dmod_dmfmc_port_api(1.0, int, _finish_sdram_initialization,
    ( dmfmc_sdram_bank_t bank, const dmfmc_sdram_chip_params_t *chip ) );

/* --- NOR / NAND / PSRAM configuration ---
 *
 * Declared for API completeness; the STM32 port currently returns -ENOSYS
 * for all three. Implement alongside a dmfmc_memory_type_nor/nand/psram
 * chip database entry when support is added. */

dmod_dmfmc_port_api(1.0, int, _configure_nor,   ( dmfmc_sdram_bank_t bank, dmfmc_data_bus_width_t data_bus_width ) );
dmod_dmfmc_port_api(1.0, int, _configure_nand,  ( dmfmc_sdram_bank_t bank, dmfmc_data_bus_width_t data_bus_width ) );
dmod_dmfmc_port_api(1.0, int, _configure_psram, ( dmfmc_sdram_bank_t bank, dmfmc_data_bus_width_t data_bus_width ) );

/* --- Direct memory access --- */

dmod_dmfmc_port_api(1.0, int, _get_memory_region,
    ( dmfmc_sdram_bank_t bank, void **out_address, uint32_t *out_size ) );

/* --- Interrupt handler registration --- */

dmod_dmfmc_port_api(1.0, int, _add_interrupt_handler,
    ( dmfmc_port_interrupt_handler_t handler, void *user_ptr ) );
dmod_dmfmc_port_api(1.0, int, _remove_interrupt_handler,
    ( void *user_ptr ) );

/* --- Timing helper ---
 *
 * Busy-wait for approximately the given number of microseconds, calibrated
 * from the current AHB clock. Used by chip JEDEC bring-up sequences
 * (see dmfmc_chips.c) for the sub-millisecond delays the standard requires
 * between commands; dmosi_thread_sleep() is not usable there because SDRAM
 * bring-up runs before the RTOS scheduler is started. */

dmod_dmfmc_port_api(1.0, void, _busy_wait_us, ( uint32_t us ) );

#endif // DMFMC_PORT_H
