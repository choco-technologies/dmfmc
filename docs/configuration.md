# DMFMC Configuration Reference

See [../configs/README.md](../configs/README.md) for the full INI key table
and a complete board example. Summary:

```ini
[sdram]
driver_name=dmfmc
driver_order=2          ; must come after every dmgpio pin section for this bus
memory_type=sdram       ; sdram | psram | nor | nand (only sdram is implemented)
chip=MT48LC4M32B2       ; looked up via dmfmc_chips_find()
bank=1                  ; 1 or 2 (FMC SDRAM bank)
data_bus_width=32       ; 0 (chip default), 8, 16 or 32
timeout_ms=3000         ; upper bound on the whole configuration sequence
heap_usage=heap         ; none | heap
heap_alignment=4        ; only used when heap_usage=heap
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
