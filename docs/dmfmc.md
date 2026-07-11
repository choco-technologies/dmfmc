# DMFMC Architecture

## Why FMC needs a driver split at all

The FMC SDRAM controller is a single peripheral (no multiple independent
"instances" the way UART or GPIO have), but it still splits into
`dmfmc` (common) and `dmfmc_port` (MCU-family specific), for the same reason
every other dmod hardware driver does: the *register layout and bring-up
sequence* of the FMC controller is MCU-specific, but the *chip database,
JEDEC command sequencing logic, dmdrvi/dmheap integration, and IOCTL surface*
are not. Keeping that logic in `dmfmc` means adding STM32H7 or another family
later only requires a new `dmfmc_port`, not touching the chip database or the
dmdrvi contract.

## Division of responsibility

```
dmfmc (common)                          dmfmc_port (per MCU family)
─────────────────                       ────────────────────────────
dmdrvi contract (_create/_read/...)     FMC_SDCRx/SDTRx/SDCMR/SDRTR/SDSR
INI parsing, chip lookup                register access
dmheap_init() integration               RCC clock enable
dmhaman interrupt dispatch              NVIC IRQ registration
                                         HCLK -> SDCLK divider selection
src/dmfmc_chips.c (common)
─────────────────────────
Per-chip timing parameters (from datasheet)
JEDEC bring-up sequence (calls dmfmc_port_sdram_send_command /
dmfmc_port_busy_wait_us - never touches registers directly)
```

Unlike `dmuart`/`dmgpio`, `dmfmc` does **not** configure its own GPIO pins.
An FMC bus uses far too many pins (32 data lines alone on a 32-bit SDRAM) to
represent as a single "pins" struct the way UART's TX/RX or a GPIO driver's
single pin can - and every one of those pins is *already* a `dmgpio` config
section elsewhere in the same INI file, ordered before the `dmfmc` section
via `driver_order` (see `configs/board/*/sdram.ini`). `dmdevfs` configures
them in file order before configuring `dmfmc`, exactly like it does for
`dmuart`'s TX/RX pins.

## Configuration flow (SDRAM)

1. `dmdevfs` loads the module and calls `dmfmc_dmdrvi_create()` with the
   `[sdram]` INI section.
2. The section is parsed into a `dmfmc_config_t`; the `chip` key is resolved
   to a `dmfmc_chip_info_t` via `dmfmc_chips_find()`.
3. `dmfmc_port_init()` enables the FMC's AHB clock.
4. `dmfmc_port_configure_sdram()` derives the SDCLK divider (fastest of
   HCLK/2, HCLK/3 that stays under the chip's rated frequency), converts every
   nanosecond timing parameter to SDCLK cycles, and programs
   `FMC_SDCRx`/`FMC_SDTRx`. It reports back the actual frequency, CAS latency
   and mapped address/size in a `dmfmc_sdram_port_result_t`.
5. The chip's `init_function` (e.g. `initialize_mt48lc4m32b2` in
   `dmfmc_chips.c`) runs the JEDEC bring-up sequence (clock enable, wait,
   precharge-all, two auto-refresh cycles, load mode register) purely through
   `dmfmc_port_sdram_send_command()` / `dmfmc_port_busy_wait_us()` - it never
   touches a register directly, so it is identical on every MCU family.
6. `dmfmc_port_finish_sdram_initialization()` programs the refresh timer
   (`FMC_SDRTR`) from the chip's `auto_refresh_period_us`.
7. If `heap_usage=heap`, the mapped region is handed to `dmheap_init()` as a
   brand new heap context (dmheap supports any number of independent
   contexts - see its README for the same `kernel_ctx`/`extram_ctx` pattern).

## Why bring-up runs before the RTOS scheduler starts

`dmdevfs` configures drivers while mounting `/dev`, which in `dmod-boot`
happens *before* `dmosi_init()` starts the RTOS scheduler (see
`dmod-boot/src/main.c`). `dmosi_thread_sleep()` would not yield a real delay
that early in boot, so the microsecond-scale delays the JEDEC sequence needs
(e.g. "wait >=100us before the first command") go through
`dmfmc_port_busy_wait_us()`, a clock-calibrated spin loop, instead.

## Scope of this first version

Only `dmfmc_memory_type_sdram` is implemented. `dmfmc_memory_type_psram`,
`_nor` and `_nand` are declared throughout the contract (config, port API,
`dmfmc_chip_info_t.memory_type`) so a later chip database entry and a handful
of new `dmfmc_port_configure_*` bodies are the only things needed to support
them - `configure()` in `dmfmc.c` already rejects any other memory type
with `-ENOSYS` at the `dmdrvi_create` stage, so misconfiguration fails loudly
at boot rather than silently doing nothing.
