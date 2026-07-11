#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmfmc.h"
#include "dmfmc_port.h"
#include "dmfmc_chips.h"
#include "dmdrvi.h"
#include "dmhaman.h"
#include "dmini.h"
#include "dmheap.h"
#include <errno.h>
#include <string.h>

#define DMFMC_CONTEXT_MAGIC    0x444D4643  /* 'DMFC' */

/**
 * @brief DMDRVI context structure
 */
struct dmdrvi_context
{
    uint32_t                    magic;                    /**< Magic number for validation */
    dmfmc_config_t               config;                   /**< Configuration parameters */
    dmfmc_sdram_port_result_t    result;                   /**< Result reported back by the port layer */
    dmheap_context_t            *heap_ctx;                 /**< dmheap context (only when config.heap_usage requests it) */
    char                        *interrupt_handler_name;   /**< dmhaman handler name (NULL = not used) */
};

static int is_valid_context(dmdrvi_context_t context)
{
    return (context != NULL && context->magic == DMFMC_CONTEXT_MAGIC);
}

/* ---- Interrupt dispatch ---- */

/* Dispatches port interrupt events to a dmhaman-registered handler. Used for
 * the handler configured via the "interrupt_handler" INI key; a handler set
 * at runtime via dmfmc_ioctl_cmd_set_interrupt_handler is instead registered
 * directly with the port layer (see the ioctl implementation below). */
static void internal_interrupt_handler(void *user_ptr, dmfmc_sdram_bank_t bank, dmfmc_interrupt_event_t event)
{
    dmdrvi_context_t ctx = (dmdrvi_context_t)user_ptr;

    if (ctx->interrupt_handler_name != NULL)
    {
        dmfmc_interrupt_params_t params;
        params.bank  = bank;
        params.event = event;
        dmhaman_call_handler(ctx->interrupt_handler_name, &params);
    }
}

/* ---- String conversion helpers ---- */

static dmfmc_memory_type_t string_to_memory_type(const char *s)
{
    if (s != NULL)
    {
        if (strcmp(s, "psram") == 0) return dmfmc_memory_type_psram;
        if (strcmp(s, "nor")   == 0) return dmfmc_memory_type_nor;
        if (strcmp(s, "nand")  == 0) return dmfmc_memory_type_nand;
    }
    return dmfmc_memory_type_sdram;
}

static dmfmc_sdram_bank_t int_to_bank(int val)
{
    return (val == 2) ? dmfmc_sdram_bank_2 : dmfmc_sdram_bank_1;
}

static dmfmc_data_bus_width_t int_to_data_bus_width(int val)
{
    switch (val)
    {
        case 8:  return dmfmc_data_bus_width_8;
        case 16: return dmfmc_data_bus_width_16;
        case 32: return dmfmc_data_bus_width_32;
        default: return dmfmc_data_bus_width_default;
    }
}

static dmfmc_heap_usage_t string_to_heap_usage(const char *s)
{
    if (s != NULL && strcmp(s, "heap") == 0) return dmfmc_heap_usage_use_as_heap;
    return dmfmc_heap_usage_none;
}

/* ---- Configuration ---- */

/**
 * @brief Detect which INI section holds the FMC configuration.
 *
 * Mirrors dmuart's section auto-detection: prefer a [dmfmc] section, but
 * fall back to scanning for the first section (other than [main]) that
 * carries a "chip" or "bank" key, so board configs can name the section
 * after the physical memory (e.g. [sdram]) instead of the driver.
 */
static const char *detect_config_section(dmini_context_t ini, char *section_buf, size_t section_buf_sz)
{
    if (dmini_has_key(ini, "dmfmc", "chip") || dmini_has_key(ini, "dmfmc", "bank"))
        return "dmfmc";

    int needed = dmini_generate_string(ini, NULL, 0);
    if (needed <= 1)
        return "dmfmc";

    char *ini_str = (char *)Dmod_Malloc((size_t)needed + 1);
    if (ini_str == NULL)
        return "dmfmc";

    if (dmini_generate_string(ini, ini_str, (size_t)needed + 1) <= 0)
    {
        Dmod_Free(ini_str);
        return "dmfmc";
    }
    ini_str[needed] = '\0';

    const char *result = "dmfmc";
    char *p = ini_str;
    while (*p != '\0')
    {
        if (*p == '[')
        {
            char *name_start = p + 1;
            char *name_end   = name_start;
            while (*name_end != '\0' && *name_end != ']' &&
                   *name_end != '\n'  && *name_end != '\r')
                name_end++;

            if (*name_end == ']')
            {
                size_t name_len = (size_t)(name_end - name_start);
                if (name_len > 0 && name_len < section_buf_sz)
                {
                    memcpy(section_buf, name_start, name_len);
                    section_buf[name_len] = '\0';

                    if (strcmp(section_buf, "main") != 0 &&
                        (dmini_has_key(ini, section_buf, "chip") ||
                         dmini_has_key(ini, section_buf, "bank")))
                    {
                        result = section_buf;
                        break;
                    }
                }
            }
        }
        p++;
    }

    Dmod_Free(ini_str);
    return result;
}

static int read_config_parameters(dmdrvi_context_t context, dmini_context_t config)
{
    char section_buf[64];
    const char *section = detect_config_section(config, section_buf, sizeof(section_buf));

    context->config.memory_type = string_to_memory_type(dmini_get_string(config, section, "memory_type", "sdram"));
    context->config.bank        = int_to_bank(dmini_get_int(config, section, "bank", 1));
    context->config.data_bus_width = int_to_data_bus_width(dmini_get_int(config, section, "data_bus_width", 0));
    context->config.configuration_timeout_ms = (uint32_t)dmini_get_int(config, section, "timeout_ms", 3000);
    context->config.heap_usage     = string_to_heap_usage(dmini_get_string(config, section, "heap_usage", "none"));
    context->config.heap_alignment = (uint32_t)dmini_get_int(config, section, "heap_alignment", (int)sizeof(void *));
    context->config.interrupt_handler = NULL;

    const char *chip_name = dmini_get_string(config, section, "chip", NULL);
    context->config.chip = dmfmc_chips_find(chip_name);
    if (context->config.chip == NULL)
    {
        DMOD_LOG_ERROR("FMC: unknown or missing chip '%s'\n", (chip_name != NULL) ? chip_name : "(none)");
        return -EINVAL;
    }

    const char *handler_name = dmini_get_string(config, section, "interrupt_handler", NULL);
    context->interrupt_handler_name = (handler_name != NULL) ? Dmod_StrDup(handler_name) : NULL;

    return 0;
}

/**
 * @brief Apply configuration to the port layer
 */
static int configure(dmdrvi_context_t context)
{
    dmfmc_config_t *c = &context->config;
    int ret;

    if (c->memory_type != dmfmc_memory_type_sdram)
    {
        DMOD_LOG_ERROR("FMC: memory type not implemented yet (only SDRAM is currently supported)\n");
        return -ENOSYS;
    }

    ret = dmfmc_port_init();
    if (ret != 0)
    {
        DMOD_LOG_ERROR("FMC: failed to initialize port\n");
        return ret;
    }

    ret = dmfmc_port_configure_sdram(c->bank, c->data_bus_width, &c->chip->params.sdram, &context->result);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("FMC: failed to configure SDRAM controller for bank %u\n", (unsigned)c->bank);
        return ret;
    }

    if (c->chip->init_function != NULL)
    {
        ret = c->chip->init_function(c->bank, &c->chip->params.sdram, &context->result);
        if (ret != 0)
        {
            DMOD_LOG_ERROR("FMC: chip bring-up sequence failed for '%s'\n", c->chip->name);
            dmfmc_port_unconfigure_sdram(c->bank);
            return ret;
        }
    }

    ret = dmfmc_port_finish_sdram_initialization(c->bank, &c->chip->params.sdram);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("FMC: failed to program the refresh timer\n");
        dmfmc_port_unconfigure_sdram(c->bank);
        return ret;
    }

    if (context->interrupt_handler_name != NULL)
    {
        if (dmfmc_port_add_interrupt_handler(internal_interrupt_handler, context) != 0)
            DMOD_LOG_ERROR("FMC: failed to register interrupt handler\n");
    }

    context->heap_ctx = NULL;
    if (c->heap_usage == dmfmc_heap_usage_use_as_heap)
    {
        context->heap_ctx = dmheap_init(context->result.memory_start, context->result.memory_size_bytes, c->heap_alignment);
        if (context->heap_ctx == NULL)
            DMOD_LOG_ERROR("FMC: failed to register SDRAM as an additional heap\n");
    }

    DMOD_LOG_INFO("FMC: '%s' configured on bank %u: %u bytes @ %p, %u Hz\n",
        c->chip->name, (unsigned)c->bank, (unsigned)context->result.memory_size_bytes,
        context->result.memory_start, (unsigned)context->result.configured_frequency_hz);

    return 0;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    DMOD_LOG_INFO("DMFMC interface module initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMFMC interface module deinitialized\n");
    return 0;
}

/* ---- DMDRVI interface ---- */

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, dmdrvi_context_t, _create, ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    if (config == NULL || dev_num == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters to dmfmc_dmdrvi_create\n");
        return NULL;
    }

    dmdrvi_context_t context = Dmod_Malloc(sizeof(struct dmdrvi_context));
    if (context == NULL)
        return NULL;

    memset(context, 0, sizeof(*context));
    context->magic = DMFMC_CONTEXT_MAGIC;

    if (read_config_parameters(context, config) != 0 ||
        configure(context) != 0)
    {
        DMOD_LOG_ERROR("Failed to create DMDRVI context with provided configuration\n");
        Dmod_Free(context->interrupt_handler_name);
        Dmod_Free(context);
        return NULL;
    }

    /* One FMC controller per chip; the bank being driven is the natural
     * major number (a second dmfmc instance driving the other bank gets a
     * distinct major). */
    dev_num->flags = DMDRVI_NUM_MAJOR;
    dev_num->major = (dmdrvi_dev_id_t)context->config.bank;

    /* If the config uses a named section (e.g. [sdram]) populate alt_name so
     * the device filesystem registers the device under that human-friendly
     * name instead of a numeric path. */
    char section_buf[DMDRVI_ALT_NAME_MAX_LEN + 1];
    const char *section = detect_config_section(config, section_buf, sizeof(section_buf));
    if (strcmp(section, "dmfmc") != 0)
    {
        size_t name_len = strlen(section);
        if (name_len <= DMDRVI_ALT_NAME_MAX_LEN)
        {
            dev_num->flags |= DMDRVI_NUM_ALT_NAME;
            memcpy(dev_num->alt_name, section, name_len + 1);
        }
    }

    return context;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, void, _free, ( dmdrvi_context_t context ))
{
    if (is_valid_context(context))
    {
        if (context->interrupt_handler_name != NULL || context->config.interrupt_handler != NULL)
            dmfmc_port_remove_interrupt_handler(context);

        dmfmc_port_unconfigure_sdram(context->config.bank);

        /* dmheap has no teardown API - a heap context handed out via
         * dmfmc_ioctl_cmd_get_heap_context outlives this driver instance by
         * design, so it is intentionally not released here. */

        Dmod_Free(context->interrupt_handler_name);
        context->magic = 0;
        Dmod_Free(context);
    }
}

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, void*, _open, ( dmdrvi_context_t context, int flags ))
{
    if (!is_valid_context(context))
    {
        DMOD_LOG_ERROR("Invalid DMDRVI context in dmfmc_dmdrvi_open\n");
        return NULL;
    }
    return context;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, void, _close, ( dmdrvi_context_t context, void* handle ))
{
    /* No specific action needed to close the FMC device handle */
}

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, size_t, _read, ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, uint32_t offset ))
{
    if (!is_valid_context(context) || buffer == NULL || offset >= context->result.memory_size_bytes)
        return 0;

    size_t available = context->result.memory_size_bytes - offset;
    size_t to_copy = (size < available) ? size : available;

    memcpy(buffer, (const uint8_t *)context->result.memory_start + offset, to_copy);
    return to_copy;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, size_t, _write, ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, uint32_t offset ))
{
    if (!is_valid_context(context) || buffer == NULL || offset >= context->result.memory_size_bytes)
        return 0;

    size_t available = context->result.memory_size_bytes - offset;
    size_t to_copy = (size < available) ? size : available;

    memcpy((uint8_t *)context->result.memory_start + offset, buffer, to_copy);
    return to_copy;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, int, _ioctl, ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    if (!is_valid_context(context))
    {
        DMOD_LOG_ERROR("Invalid DMDRVI context in dmfmc_dmdrvi_ioctl\n");
        return -EINVAL;
    }

    if (command >= dmfmc_ioctl_cmd_max)
    {
        DMOD_LOG_ERROR("Invalid ioctl command %d\n", command);
        return -EINVAL;
    }

    switch (command)
    {
        case dmfmc_ioctl_cmd_get_memory_start:
            if (arg == NULL) return -EINVAL;
            *(void **)arg = context->result.memory_start;
            return 0;

        case dmfmc_ioctl_cmd_get_memory_size:
            if (arg == NULL) return -EINVAL;
            *(uint32_t *)arg = context->result.memory_size_bytes;
            return 0;

        case dmfmc_ioctl_cmd_get_configured_frequency:
            if (arg == NULL) return -EINVAL;
            *(uint32_t *)arg = context->result.configured_frequency_hz;
            return 0;

        case dmfmc_ioctl_cmd_get_heap_context:
            if (arg == NULL) return -EINVAL;
            *(dmheap_context_t **)arg = context->heap_ctx;
            return 0;

        case dmfmc_ioctl_cmd_set_interrupt_handler:
            if (arg == NULL)
            {
                dmfmc_port_remove_interrupt_handler(context);
                return 0;
            }
            return dmfmc_port_add_interrupt_handler(
                (dmfmc_port_interrupt_handler_t)*(dmfmc_interrupt_handler_t *)arg,
                context);

        case dmfmc_ioctl_cmd_reconfigure:
            dmfmc_port_unconfigure_sdram(context->config.bank);
            return configure(context);

        default:
            return -EINVAL;
    }
}

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    if (!is_valid_context(context))
    {
        DMOD_LOG_ERROR("Invalid DMDRVI context in dmfmc_dmdrvi_flush\n");
        return -EINVAL;
    }

    /* Memory-mapped, nothing buffered by this driver. */
    return 0;
}

dmod_dmdrvi_dif_api_declaration(1.0, dmfmc, int, _stat, ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    if (!is_valid_context(context) || stat == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters in dmfmc_dmdrvi_stat\n");
        return -EINVAL;
    }

    stat->size = context->result.memory_size_bytes;
    stat->mode = 0666;
    return 0;
}
