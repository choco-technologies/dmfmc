#ifndef DMFMC_TYPES_H
#define DMFMC_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Type of memory attached to the FMC
 *
 * Bits 6-7 classify the memory as RAM or Flash; the low nibble distinguishes
 * the specific memory family within that class. Only dmfmc_memory_type_sdram
 * is fully implemented - the others are reserved for future ports.
 */
typedef enum
{
    dmfmc_memory_type_ram   = (1 << 6),
    dmfmc_memory_type_flash = (1 << 7),
    dmfmc_memory_type_sdram = 0x1 | dmfmc_memory_type_ram,
    dmfmc_memory_type_psram = 0x2 | dmfmc_memory_type_ram,
    dmfmc_memory_type_nand  = 0x3 | dmfmc_memory_type_flash,
    dmfmc_memory_type_nor   = 0x4 | dmfmc_memory_type_flash,
} dmfmc_memory_type_t;

/**
 * @brief Width of the external memory data bus, in bytes
 */
typedef enum
{
    dmfmc_data_bus_width_default = 0,  /**< Use the chip's native bus width */
    dmfmc_data_bus_width_8       = 1,
    dmfmc_data_bus_width_16      = 2,
    dmfmc_data_bus_width_32      = 4,
} dmfmc_data_bus_width_t;

/**
 * @brief FMC SDRAM bank selection
 *
 * STM32 FMC exposes two SDRAM banks (bank 1 at 0xC0000000, bank 2 at
 * 0xD0000000). A chip larger than one bank's address window is split evenly
 * across both banks by the controller itself, not by this driver.
 */
typedef enum
{
    dmfmc_sdram_bank_1 = 1,
    dmfmc_sdram_bank_2 = 2,
} dmfmc_sdram_bank_t;

/**
 * @brief Mask of CAS latencies a chip supports
 */
typedef enum
{
    dmfmc_cas_latency_1 = (1 << 0),
    dmfmc_cas_latency_2 = (1 << 1),
    dmfmc_cas_latency_3 = (1 << 2),
} dmfmc_cas_latency_t;

/**
 * @brief JEDEC-level SDRAM commands accepted by FMC_SDCMR
 */
typedef enum
{
    dmfmc_sdram_command_normal = 0,          /**< No command mode / normal operation */
    dmfmc_sdram_command_enable_clock,        /**< Provide a stable clock to the SDRAM */
    dmfmc_sdram_command_precharge_all,       /**< Precharge all banks */
    dmfmc_sdram_command_auto_refresh,        /**< Issue one or more auto-refresh cycles */
    dmfmc_sdram_command_load_mode_register,  /**< Program the SDRAM mode register */
    dmfmc_sdram_command_self_refresh,        /**< Enter self-refresh mode */
    dmfmc_sdram_command_power_down,          /**< Enter power-down mode */
} dmfmc_sdram_command_t;

/**
 * @brief Extra data required by some SDRAM commands
 */
typedef struct
{
    uint8_t  number_of_auto_refresh;  /**< dmfmc_sdram_command_auto_refresh: refresh cycles to issue (1-16) */
    uint16_t mode_register;           /**< dmfmc_sdram_command_load_mode_register: raw JEDEC mode register value */
} dmfmc_sdram_command_data_t;

/**
 * @brief Timing and capability parameters of a specific SDRAM chip
 *
 * All delay fields are in nanoseconds and all periods in microseconds unless
 * noted otherwise. These map directly to the values found in the chip's
 * datasheet timing table.
 */
typedef struct
{
    uint32_t                size_bytes;                     /**< Total chip capacity */
    uint32_t                bank_size_bytes;                /**< Size of a single internal bank */
    uint32_t                number_of_banks;                /**< Number of internal banks (chip-side, not FMC banks) */
    dmfmc_data_bus_width_t  data_bus_width;                 /**< Native data bus width */
    dmfmc_cas_latency_t     cas_latency_mask;                /**< CAS latencies the chip supports */
    uint32_t                cas_latency_ns;                  /**< CAS latency, in nanoseconds */
    bool                    auto_precharge_possible;         /**< Chip supports auto-precharge */
    bool                    auto_refresh_possible;           /**< Chip supports auto-refresh in normal operation */
    bool                    self_refresh_possible;           /**< Chip supports self-refresh during power-down */
    uint32_t                auto_refresh_period_us;          /**< Required period between auto-refresh cycles */
    uint32_t                maximum_clock_frequency_hz;      /**< Maximum SDCLK frequency the chip tolerates */
    uint8_t                 number_of_row_address_bits;      /**< Row address width */
    uint8_t                 number_of_column_address_bits;   /**< Column address width */
    uint32_t                active_to_read_write_delay_ns;   /**< tRCD */
    uint32_t                precharge_delay_ns;              /**< tRP */
    uint32_t                write_recovery_delay_ns;         /**< tWR */
    uint32_t                refresh_to_activate_delay_ns;    /**< tRC */
    uint32_t                min_self_refresh_period_ns;      /**< tRAS */
    uint32_t                exit_self_refresh_delay_ns;      /**< tXSR */
    uint32_t                cycles_to_delay_after_load_mode; /**< tMRD, in memory clock cycles */
    bool                    use_burst_read;                  /**< Always issue reads as bursts */
} dmfmc_sdram_chip_params_t;

/**
 * @brief Opaque driver context type (forward declaration)
 */
struct dmdrvi_context;
typedef struct dmdrvi_context *dmdrvi_context_t;

/**
 * @brief Result of a successful SDRAM bank configuration, filled in by the port layer
 */
typedef struct
{
    uint32_t                configured_frequency_hz; /**< Actual SDCLK frequency programmed */
    uint32_t                cas_latency_cycles;       /**< CAS latency the port layer actually programmed (1-3 SDCLK cycles) */
    void                    *memory_start;            /**< Base address of the mapped bank */
    uint32_t                memory_size_bytes;        /**< Usable size of the mapped bank */
    dmfmc_data_bus_width_t  data_bus_width;           /**< Data bus width actually configured */
} dmfmc_sdram_port_result_t;

/**
 * @brief Chip-specific JEDEC initialization sequence
 *
 * Called once by the common driver right after the FMC controller has been
 * programmed with the chip's timing, to bring the chip itself out of reset
 * (clock enable, precharge-all, auto-refresh cycles, load mode register).
 * The mode register value written to the chip must encode the same CAS
 * latency the port layer actually configured (result->cas_latency_cycles),
 * or the controller and the chip will disagree about read timing.
 *
 * @param bank      Bank the chip was configured on
 * @param params    Chip parameters (same struct passed to configuration)
 * @param result    Result reported back by dmfmc_port_configure_sdram()
 * @return 0 on success, negative errno on failure
 */
typedef int (*dmfmc_chip_init_function_t)(dmfmc_sdram_bank_t bank, const dmfmc_sdram_chip_params_t *params,
                                           const dmfmc_sdram_port_result_t *result);

/**
 * @brief Describes a known external memory chip
 */
typedef struct
{
    dmfmc_memory_type_t  memory_type;    /**< Memory family (only sdram is currently usable) */
    const char           *name;          /**< Chip name, matched against the "chip" INI key */
    union
    {
        dmfmc_sdram_chip_params_t sdram; /**< Valid when memory_type == dmfmc_memory_type_sdram */
    } params;
    dmfmc_chip_init_function_t init_function; /**< JEDEC bring-up sequence, may be NULL */
} dmfmc_chip_info_t;

/**
 * @brief Whether the memory configured by this driver instance should also
 *        be registered as an additional dmheap context
 */
typedef enum
{
    dmfmc_heap_usage_none = 0,     /**< Do not use the memory as a heap */
    dmfmc_heap_usage_use_as_heap,  /**< Register the mapped region with dmheap_init() */
} dmfmc_heap_usage_t;

/**
 * @brief IOCTL commands for the DMFMC device
 */
typedef enum
{
    dmfmc_ioctl_cmd_get_memory_start = 1,   /**< arg = void** - direct pointer to the mapped memory */
    dmfmc_ioctl_cmd_get_memory_size,        /**< arg = uint32_t* - size in bytes of the mapped memory */
    dmfmc_ioctl_cmd_get_configured_frequency, /**< arg = uint32_t* - SDCLK frequency actually configured (Hz) */
    dmfmc_ioctl_cmd_get_heap_context,       /**< arg = dmheap_context_t** - heap context, or NULL if not used as heap */
    dmfmc_ioctl_cmd_set_interrupt_handler,  /**< arg = dmfmc_interrupt_handler_t*, NULL to remove */
    dmfmc_ioctl_cmd_reconfigure,            /**< Reconfigure FMC with the current settings */

    dmfmc_ioctl_cmd_max
} dmfmc_ioctl_cmd_t;

/**
 * @brief Reason an FMC interrupt fired
 */
typedef enum
{
    dmfmc_interrupt_event_refresh_error = (1 << 0), /**< SDRAM auto-refresh could not complete in time */
} dmfmc_interrupt_event_t;

/**
 * @brief Parameters passed to a dmhaman-registered interrupt handler
 */
typedef struct
{
    dmfmc_sdram_bank_t       bank;   /**< Bank that raised the interrupt */
    dmfmc_interrupt_event_t  event;  /**< Which event fired */
} dmfmc_interrupt_params_t;

/**
 * @brief FMC interrupt handler function type (dmhaman-facing)
 */
typedef void (*dmfmc_interrupt_handler_t)(dmdrvi_context_t context, dmfmc_sdram_bank_t bank, dmfmc_interrupt_event_t event);

/**
 * @brief FMC port interrupt handler function type
 *
 * Called by the port layer when an FMC interrupt occurs.
 *
 * @param user_ptr  User pointer supplied at registration time (e.g. driver context)
 * @param bank      Bank that raised the interrupt
 * @param event     Which event fired
 */
typedef void (*dmfmc_port_interrupt_handler_t)(void *user_ptr, dmfmc_sdram_bank_t bank, dmfmc_interrupt_event_t event);

#endif /* DMFMC_TYPES_H */
