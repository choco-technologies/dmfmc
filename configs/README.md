# DMFMC Configuration Files

This directory contains pre-configured FMC (external memory) settings for
development boards with an SDRAM chip wired to the FMC.

## Directory Structure

```
configs/
└── board/                          # Board-specific configurations
    └── stm32f746g-disco/            # STM32F746G-DISCO
        └── sdram.ini
```

Unlike `dmuart`/`dmgpio`, there is no `mcu/` directory here: which FMC bank a
chip is wired to, and which GPIO pins carry which FMC signal, is a decision
made by the board's schematic, not by the MCU alone - so a per-MCU default
config would not be meaningful. Configs live only under `board/`.

## Configuration Format

Each config file contains:
- One `[sdram_*]` GPIO section per FMC signal pin (`driver_name=dmgpio`,
  alternate function mode), with `driver_order=1` so they configure first.
- One driver section with `driver_name=dmfmc` and `driver_order=2`, so
  `dmdevfs` configures every pin before bringing up the FMC controller
  itself - the same convention `dmuart` uses for its TX/RX pins.

### Example (stm32f746g-disco/sdram.ini, abridged)

```ini
[sdram_sdclk]
driver_name=dmgpio
driver_order=1
pin=PG8
mode=alternate
alternate_function=12
speed=maximum
output_circuit=push_pull

; ... one such section per FMC pin (SDCKE0, SDNE0, SDNRAS, SDNCAS, SDNWE,
; BA0-1, A0-11, D0-15, NBL0-3 - D16-D31 are deliberately NOT muxed to FMC on
; this board, see below) ...

[sdram]
driver_name=dmfmc
driver_order=2
memory_type=sdram
chip=MT48LC4M32B2
bank=1
data_bus_width=16
timeout_ms=3000
heap_usage=heap
heap_alignment=4
```

**Why 16-bit, not the chip's native 32-bit:** on the STM32F746G-Discovery,
MCU pins PH8-15/PI0-3/PI6-7/PI9-10 (which would carry FMC_D16-D31) are used
for other on-board peripherals, not wired to the SDRAM chip. Configuring
`data_bus_width=32` here would make the FMC controller drive/expect data on
pins that aren't actually connected to the chip, corrupting every access
with whatever floats on those disconnected lines. This was confirmed against
a previously working driver for this exact board, which mux only D0-D15 and
explicitly force 16-bit width for the same reason.

## Configuration Keys (`[sdram]` section)

| Key                | Values                          | Default | Meaning |
|--------------------|----------------------------------|---------|---------|
| `memory_type`      | `sdram`, `psram`, `nor`, `nand`  | `sdram` | Only `sdram` is currently implemented; the others report an error. |
| `chip`             | name from `dmfmc_chips.h`        | -       | Required. Looked up via `dmfmc_chips_find()`. |
| `bank`             | `1`, `2`                         | `1`     | Which FMC SDRAM bank the chip is wired to. |
| `data_bus_width`   | `0`, `8`, `16`, `32`             | `0`     | `0` uses the chip's native bus width. |
| `timeout_ms`       | integer                         | `3000`  | Upper bound on the whole configuration sequence. |
| `heap_usage`       | `none`, `heap`                   | `none`  | `heap` registers the mapped SDRAM as an additional dmheap context. |
| `heap_alignment`   | integer (bytes)                 | `sizeof(void*)` | Alignment used when registering the dmheap context. |
| `interrupt_handler`| dmhaman handler name             | -       | Optional: called on SDRAM refresh-error interrupts. |

## Board Configurations

| Board | Folder | Chip | Bank | Bus Width |
|-------|--------|------|------|-----------|
| STM32F746G-DISCO | `board/stm32f746g-disco/` | MT48LC4M32B2 (128 Mbit) | 1 | 16-bit (board-limited; chip is natively 32-bit) |
