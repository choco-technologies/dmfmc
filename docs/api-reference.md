# DMFMC API Reference

## dmdrvi contract (`dmfmc.h`, `dmfmc_types.h`)

`dmfmc` implements the standard dmod driver interface (`dmdrvi`), so it is
usable either directly or transparently through `dmdevfs`/`/dev`.

| Function | Behaviour |
|----------|-----------|
| `dmfmc_dmdrvi_create(config, dev_num)` | Parses the INI section, resolves the chip, configures the FMC and (optionally) registers the mapped memory with dmheap. `dev_num->major` is the FMC bank number. |
| `dmfmc_dmdrvi_free(ctx)` | Tears down interrupt registration and unconfigures the bank. The dmheap context, if created, is **not** torn down (dmheap has no teardown API) and outlives the driver instance. |
| `dmfmc_dmdrvi_open` / `_close` | Trivial - the context itself is used as the handle. |
| `dmfmc_dmdrvi_read(ctx, handle, buffer, size, offset)` | `memcpy()` from the mapped memory at `offset`, clamped to the configured size. |
| `dmfmc_dmdrvi_write(ctx, handle, buffer, size, offset)` | `memcpy()` to the mapped memory at `offset`, clamped to the configured size. |
| `dmfmc_dmdrvi_flush` | No-op (memory-mapped, nothing buffered). |
| `dmfmc_dmdrvi_stat` | `size` = configured memory size, `mode` = 0666. |
| `dmfmc_dmdrvi_ioctl` | See IOCTL commands below. |

## IOCTL commands (`dmfmc_ioctl_cmd_t`)

Commands are numbered from `DMDRVI_IOCTL_CUSTOM_BASE` (0x1000) so they do not
collide with the standard `DMDRVI_IOCTL_*` commands. Any command outside this
range (e.g. the `DMDRVI_IOCTL_BLOCK_GET_INFO` / `DMDRVI_IOCTL_MONITOR_GET_POLICY`
probes sent by dmdevfs) is answered with `-ENOTTY`.

| Command | `arg` type | Meaning |
|---------|-----------|---------|
| `dmfmc_ioctl_cmd_get_memory_start` | `void **` | Direct pointer to the mapped memory. |
| `dmfmc_ioctl_cmd_get_memory_size` | `uint32_t *` | Size in bytes of the mapped memory. |
| `dmfmc_ioctl_cmd_get_configured_frequency` | `uint32_t *` | Actual SDCLK frequency (Hz). |
| `dmfmc_ioctl_cmd_get_heap_context` | `dmheap_context_t **` | The dmheap context, or `NULL` if `heap_usage != heap`. |
| `dmfmc_ioctl_cmd_set_interrupt_handler` | `dmfmc_interrupt_handler_t *`, or `NULL` to remove | Registers a raw callback for SDRAM refresh-error events, bypassing dmhaman. |
| `dmfmc_ioctl_cmd_reconfigure` | (unused) | Unconfigures and reconfigures the bank with the current settings. |

## Chip database (`dmfmc_chips.h`)

```c
extern const dmfmc_chip_info_t dmfmc_chip_info_mt48lc4m32b2;
const dmfmc_chip_info_t *dmfmc_chips_find(const char *name);
```

`dmfmc_chips_find()` performs a case-insensitive lookup against
`dmfmc_chip_info_t.name`, comparing against each chip descriptor known to
`src/dmfmc_chips.c` in turn (see the note in that file on why this is a
chain of comparisons rather than an array of pointers to each chip).

## Port contract (`dmfmc_port.h`)

Implemented once per MCU family in `src/port/<family>/`. Never called by
application code directly - only by `dmfmc.c` and `dmfmc_chips.c`.

| Function | Purpose |
|----------|---------|
| `dmfmc_port_init()` / `_deinit()` | Enable/reset the FMC peripheral clock. |
| `dmfmc_port_configure_sdram(bank, data_bus_width, chip, out_result)` | Programs `FMC_SDCRx`/`FMC_SDTRx` from chip timing parameters; reports the configured frequency, CAS latency, and mapped address/size. |
| `dmfmc_port_unconfigure_sdram(bank)` | Marks the bank unconfigured. |
| `dmfmc_port_sdram_send_command(bank, command, data, timeout_ms)` | Issues one JEDEC-level command (`FMC_SDCMR`) and waits for `FMC_SDSR.BUSY` to clear. |
| `dmfmc_port_finish_sdram_initialization(bank, chip)` | Programs the auto-refresh timer (`FMC_SDRTR`) from the chip's refresh period. Must be called after the chip's JEDEC bring-up completes. |
| `dmfmc_port_configure_nor/nand/psram(bank, data_bus_width)` | Declared for API completeness; the STM32 port currently returns `-ENOSYS` for all three. |
| `dmfmc_port_get_memory_region(bank, out_address, out_size)` | Returns the mapped address/size of an already-configured bank. |
| `dmfmc_port_add_interrupt_handler(handler, user_ptr)` / `_remove_interrupt_handler(user_ptr)` | Registers for SDRAM refresh-error interrupts. Single global slot - registering a new handler replaces any previous one. |
| `dmfmc_port_busy_wait_us(us)` | Clock-calibrated busy-wait, used by chip bring-up sequences for sub-millisecond JEDEC delays. |

## Chip bring-up dispatch (`dmfmc_chips_run_init_sequence`)

```c
int dmfmc_chips_run_init_sequence(const dmfmc_chip_info_t *chip, dmfmc_sdram_bank_t bank,
                                   const dmfmc_sdram_port_result_t *result);
```

Called once by `dmfmc.c`'s `configure()`, after `dmfmc_port_configure_sdram()`
has already programmed the controller. Dispatches on `chip->chip_id`
(`dmfmc_chip_id_t`) to a `static` bring-up function in `dmfmc_chips.c` via a
plain `switch` statement - **not** a stored function pointer. dmod modules
are linked as a flat, fixed-base image with no load-time relocation of data:
a function pointer baked into a `static const` initializer only holds the
right address if the module happens to load at the address it was linked
for, which does not hold in general (confirmed the hard way - see the git
history of `dmfmc_chips.c` for the crash this caused). A `switch` on an enum
compiles to an ordinary PC-relative call, which is safe regardless of where
the module ends up loaded. The same reasoning is why `dmfmc_chip_info_t.name`
is a `char[]` rather than a `const char *`, and why `dmfmc_chips_find()`
compares against each known chip directly instead of walking an array of
pointers to per-chip globals.

Each bring-up function must run its command sequence purely through
`dmfmc_port_sdram_send_command()` and `dmfmc_port_busy_wait_us()` - it must
**not** access any register directly, since that would tie the chip database
to a specific MCU family. `result->cas_latency_cycles` must be encoded into
any `LOAD MODE REGISTER` command; using a different latency than what the
controller was actually programmed with will cause silent read corruption.
