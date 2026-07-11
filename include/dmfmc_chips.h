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

#endif /* DMFMC_CHIPS_H */
