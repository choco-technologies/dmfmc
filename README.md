# DMFMC - DMOD FMC (Flexible Memory Controller) Driver Module

A [dmod](https://github.com/choco-technologies/dmod) driver module for the
STM32 FMC peripheral, providing access to external memory (currently SDRAM)
wired to the microcontroller. Loaded automatically by `dmdevfs`, the same way
[dmuart](https://github.com/choco-technologies/dmuart) and
[dmgpio](https://github.com/choco-technologies/dmgpio) are.

## Features

- SDRAM configuration (column/row/bank geometry, CAS latency, all JEDEC
  timing parameters, auto-refresh) derived entirely from a chip descriptor -
  no manual register math required to support a new SDRAM chip.
- Automatic SDCLK frequency selection (HCLK/2 or HCLK/3, whichever is the
  fastest the chip tolerates).
- Optional registration of the mapped memory as an additional `dmheap`
  context, so application code can `dmheap_malloc()` straight into external
  RAM.
- Optional SDRAM refresh-error interrupt reporting through `dmhaman`.
- Split into `dmfmc` (MCU-agnostic) and `dmfmc_port` (MCU-family specific),
  like every other dmod hardware driver.
- NOR/NAND Flash and PSRAM are declared in the port contract for future
  support, but not yet implemented (only SDRAM is).

## Quick Start

### Installation

```bash
# Create a dependencies file (deps.dmd)
echo "dmfmc board/stm32f746g-disco/sdram.ini" >> deps.dmd

# Install with configuration
dmf-get deps.dmd
```

### Basic Usage

```c
#include "dmfmc.h"
#include "dmdrvi.h"
#include "dmini.h"

dmini_context_t config = dmini_load("sdram.ini");
dmdrvi_dev_num_t dev_num = {0};

dmdrvi_context_t fmc_ctx = dmfmc_dmdrvi_create(config, &dev_num);
void *handle = dmfmc_dmdrvi_open(fmc_ctx, DMDRVI_O_RDWR);

uint8_t buffer[256];
dmfmc_dmdrvi_write(fmc_ctx, handle, buffer, sizeof(buffer), 0);
dmfmc_dmdrvi_read(fmc_ctx, handle, buffer, sizeof(buffer), 0);

void *sdram_start = NULL;
dmfmc_dmdrvi_ioctl(fmc_ctx, handle, dmfmc_ioctl_cmd_get_memory_start, &sdram_start);

dmfmc_dmdrvi_close(fmc_ctx, handle);
dmfmc_dmdrvi_free(fmc_ctx);
```

Most applications never call this API directly - `dmdevfs` creates the
context automatically from `configs/board/*/sdram.ini` at boot, and the
memory is either used directly (`/dev/sdram`, or the alt-name from the INI
section) or transparently through `dmheap_malloc()` when
`heap_usage=heap` is set.

## Building

### Prerequisites

- CMake 3.18+
- GCC-compatible cross compiler for the target MCU

### Build Commands

```bash
# Configure for STM32F7
cmake -DDMFMC_MCU_SERIES=stm32f7 -DDMOD_MODE=DMOD_MODULE -B build -S .

# Build
cmake --build build/
```

## Documentation

See [docs/](docs/) for the architecture overview, API reference,
configuration reference, and a guide for adding a new MCU port.

## Supported Platforms

| MCU Series | Status | Notes |
|-------------|--------|-------|
| STM32F7     | Implemented | Targets STM32F746G-Discovery (MT48LC4M32B2 SDRAM); under active hardware bring-up, see `configs/board/stm32f746g-disco/sdram.ini` for the board-specific 16-bit bus note. |
| STM32F4     | Implemented | Targets F42x/43x/469/479 (the F4 parts with an FMC, not just an FSMC). Not yet validated on real hardware. |

## Supported Memory Chips

| Chip | Type | Capacity | Boards |
|------|------|----------|--------|
| Micron MT48LC4M32B2 | SDRAM | 128 Mbit (4M x 32) | STM32F746G-Discovery |

## Configuration Examples

See [configs/README.md](configs/README.md) for the full INI key reference and
board configuration examples.

## Development

### Project Structure

```
dmfmc/
├── include/               # Public headers (dmfmc.h, dmfmc_port.h, dmfmc_types.h, dmfmc_chips.h)
│   └── port/              # Register definitions (stm32_common_regs.h, stm32f4_regs.h, stm32f7_regs.h)
├── src/
│   ├── dmfmc.c            # Common driver: dmdrvi contract, configuration, dmheap integration
│   ├── dmfmc_chips.c      # Known-chip database and JEDEC bring-up sequences
│   └── port/
│       ├── stm32_common/  # SDRAM controller logic shared by STM32F4 and STM32F7
│       ├── stm32f4/       # Lifecycle + IRQ registration for STM32F4
│       └── stm32f7/       # Lifecycle + IRQ registration for STM32F7
├── configs/               # Board configuration files (pins + FMC settings)
├── docs/                  # Architecture, API and configuration documentation
├── examples/              # Minimal standalone INI example
└── tests/                 # Host-buildable unit tests (chip database, no hardware required)
```

### Adding New Platform Support

See [docs/port-implementation.md](docs/port-implementation.md).

### Adding a New Memory Chip

See [docs/configuration.md](docs/configuration.md#adding-a-new-chip). In
short: add a `dmfmc_chip_info_t` entry to `src/dmfmc_chips.c` with the chip's
datasheet timing parameters and a JEDEC bring-up function, dispatched by a
new `dmfmc_chip_id_t` enum value rather than a stored function pointer (dmod
modules don't support relocating pointers embedded in static data - see
[docs/api-reference.md](docs/api-reference.md#chip-bring-up-dispatch-dmfmc_chips_run_init_sequence)).
No port-layer changes are required - `dmfmc_port` derives every register
value from the chip descriptor at configuration time.

## License

See [LICENSE](LICENSE).

## Related Projects

- [dmod](https://github.com/choco-technologies/dmod) - the module loading framework this driver plugs into
- [dmuart](https://github.com/choco-technologies/dmuart), [dmgpio](https://github.com/choco-technologies/dmgpio) - sibling drivers using the same common/port split
- [dmheap](https://github.com/choco-technologies/dmheap) - heap manager this driver can register external RAM with
