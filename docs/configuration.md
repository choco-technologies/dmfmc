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
`configs/board/stm32f746g-disco/sdram.ini` for a complete 32-bit example.

## Adding a new chip

1. Find the chip's timing table in its datasheet.
2. Add a `dmfmc_chip_info_t` entry to `src/dmfmc_chips.c`, filling in
   `dmfmc_sdram_chip_params_t` directly from the datasheet's nanosecond/
   microsecond values (no unit conversion needed - see the
   `MT48LC4M32B2` entry for the field-by-field mapping to a datasheet).
3. Write a JEDEC bring-up function for it (`dmfmc_chip_init_function_t`) -
   for most SDRAM chips this is: enable clock, wait >=100us, NOP,
   precharge-all, two auto-refresh cycles, load mode register. Reuse
   `initialize_mt48lc4m32b2()` as a template.
4. Register the new entry in `s_known_chips[]`.
5. Reference the chip by name from a board's `chip=` INI key. No `dmfmc_port`
   change is required - every register value is derived from the chip
   descriptor at configuration time.
