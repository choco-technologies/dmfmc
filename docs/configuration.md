# DMFMC Configuration Reference

See [../configs/README.md](../configs/README.md) for the full INI key table
and a complete board example. Summary:

```ini
[sdram]
driver_name=dmfmc
driver_order=-8         ; must come after every dmgpio pin section for this bus
memory_type=sdram       ; sdram | psram | nor | nand (only sdram is implemented)
chip=MT48LC4M32B2       ; looked up via dmfmc_chips_find()
bank=1                  ; 1 or 2 (FMC SDRAM bank)
data_bus_width=32       ; 0 (chip default), 8, 16 or 32
timeout_ms=3000         ; upper bound on the whole configuration sequence
heap_usage=heap         ; none | heap
heap_alignment=4        ; only used when heap_usage=heap
disable_unused_norsram_bank1=0 ; STM32F7 board opt-in; 1 only if this asynchronous bank is unused
cache=on                ; on (default) | off - let the CPU cache the memory
interrupt_handler=my_handler  ; optional dmhaman handler name for refresh-error events
```

## Section naming

Like `dmuart`, the driver section does not have to be named `[dmfmc]` - any
section containing a `chip` or `bank` key is recognised, and its name becomes
the device's alt-name in `dmdevfs` (e.g. `[sdram]` -> `/dev/sdram`). This is
what lets a board config name the section after the physical memory instead
of the driver.

## Pin configuration

`dmfmc` does not configure any GPIO pin itself. Every FMC signal (data,
address, control) must have its own `[..]` section with `driver_name=dmgpio`
elsewhere in the same INI file, using a `driver_order` lower than the
`dmfmc` section's so `dmdevfs` muxes the pins first. See
`configs/board/stm32f746g-disco/sdram.ini` for a complete example - note that
board specifically wires only a 16-bit data bus (`data_bus_width=16`) even
though the chip is natively 32-bit; D16-D31 are used for other on-board
peripherals there, so don't copy `data_bus_width=32` onto that board's pin
set without checking the target board's actual wiring first.

## Adding a new chip

1. Find the chip's timing table in its datasheet.
2. Add a `dmfmc_chip_id_t` value for it in `dmfmc_types.h`.
3. Add a `static const dmfmc_chip_info_t` entry to `src/dmfmc_chips.c`,
   filling in `dmfmc_sdram_chip_params_t` directly from the datasheet's
   nanosecond/microsecond values (no unit conversion needed - see the
   `MT48LC4M32B2` entry for the field-by-field mapping to a datasheet), and
   set `.chip_id` to the new enum value.
4. Write a JEDEC bring-up function for it (a `static int
   initialize_<chip>(dmfmc_sdram_bank_t bank, const dmfmc_sdram_chip_params_t
   *params, const dmfmc_sdram_port_result_t *result)`) - for most SDRAM chips
   this is: enable clock, wait >=100us, NOP, precharge-all, two auto-refresh
   cycles, load mode register. Reuse `initialize_mt48lc4m32b2()` as a
   template.
5. Add an `else if` branch to `dmfmc_chips_find()` comparing against the new
   entry, and a `case` to `dmfmc_chips_run_init_sequence()`'s `switch`
   calling the new bring-up function.
6. Reference the chip by name from a board's `chip=` INI key. No `dmfmc_port`
   change is required - every register value is derived from the chip
   descriptor at configuration time.

Do **not** add a function pointer field to `dmfmc_chip_info_t`, and do not
add an array of pointers to per-chip globals (e.g. `static const
dmfmc_chip_info_t *table[] = {&dmfmc_chip_info_x}`) - see
[api-reference.md](api-reference.md#chip-bring-up-dispatch-dmfmc_chips_run_init_sequence)
for why: dmod's loader does not relocate pointers baked into a module's
static data, so both patterns crash as soon as the module loads at an
address other than the one it was linked for.

## Unused NOR/SRAM bank on STM32F7

On a board without asynchronous memory on NOR/SRAM bank 1, set
`disable_unused_norsram_bank1=1` (requires dmfmc and dmfmc_port 1.3 or newer).
The default is `0`, preserving existing board configurations. The STM32F746G-DISCO
configuration enables it. This is separate from `bank=1`, which selects the
SDRAM bank; SDRAM remains enabled.

The STM32F7 port clears only BCR1.MBKEN immediately after the FMC clock is
available, before SDRAM configuration and heap registration. This prevents
speculative Cortex-M7 accesses to the unused asynchronous bank from stalling
SDRAM and starving the LTDC, as described in
[ST AN4861](https://www.st.com/resource/en/application_note/an4861-lcdtft-display-controller-ltdc-on-stm32-mcus-stmicroelectronics.pdf).
Do not enable it when the board uses NOR/SRAM on this bank. Unsupported ports
return an error instead of silently ignoring the requested setting.
