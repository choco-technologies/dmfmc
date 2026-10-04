#ifndef DMFMC_H
#define DMFMC_H

#include "dmfmc_defs.h"
#include "dmfmc_types.h"

/**
 * @brief FMC driver configuration structure
 */
typedef struct
{
    dmfmc_memory_type_t      memory_type;              /**< Type of memory attached to the FMC (only sdram is implemented) */
    dmfmc_sdram_bank_t       bank;                      /**< FMC SDRAM bank the chip is wired to */
    const dmfmc_chip_info_t  *chip;                     /**< Resolved chip parameters (looked up by name from configs) */
    dmfmc_data_bus_width_t   data_bus_width;            /**< Override for the chip's native bus width (default = chip's own) */
    uint32_t                 configuration_timeout_ms;  /**< Maximum time allowed for the whole configuration sequence */
    dmfmc_heap_usage_t       heap_usage;                /**< Whether to register the mapped memory with dmheap */
    uint32_t                 heap_alignment;            /**< Alignment to use when registering the dmheap context */
    bool                     cache;                     /**< Let the CPU cache the memory (cache=on, the default) */
    dmfmc_interrupt_handler_t interrupt_handler;        /**< Interrupt handler (NULL = not used) */
} dmfmc_config_t;

#endif /* DMFMC_H */
