#ifndef DMFMC_CHIPS_H
#define DMFMC_CHIPS_H

#include "dmfmc_types.h"

/**
 * @brief Micron MT48LC4M32B2 - 128 Mbit (4M x 32) SDRAM
 *
 * This is the external SDRAM fitted on the STM32F746G-Discovery board,
 * connected to FMC SDRAM bank 1.
 */
extern const dmfmc_chip_info_t dmfmc_chip_info_mt48lc4m32b2;

/**
 * @brief Look up a chip descriptor by name (as used in the "chip" INI key)
 *
 * Lookup is case-insensitive.
 *
 * @param name Chip name, e.g. "MT48LC4M32B2"
 * @return Pointer to the chip descriptor, or NULL if the name is unknown
 */
const dmfmc_chip_info_t *dmfmc_chips_find(const char *name);

/**
 * @brief Run a chip's JEDEC bring-up sequence
 *
 * Dispatches on chip->chip_id to the matching bring-up sequence (clock
 * enable, precharge-all, auto-refresh cycles, load mode register). Must be
 * called after dmfmc_port_configure_sdram() has already programmed the
 * controller for this bank, and before dmfmc_port_finish_sdram_initialization().
 *
 * @param chip   Chip descriptor, as returned by dmfmc_chips_find()
 * @param bank   Bank the chip was configured on
 * @param result Result reported back by dmfmc_port_configure_sdram()
 * @return 0 on success, negative errno on failure (-ENOSYS if chip->chip_id is unknown)
 */
int dmfmc_chips_run_init_sequence(const dmfmc_chip_info_t *chip, dmfmc_sdram_bank_t bank,
                                   const dmfmc_sdram_port_result_t *result);

#endif /* DMFMC_CHIPS_H */
