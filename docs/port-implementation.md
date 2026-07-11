# Adding a New MCU Port to DMFMC

`dmfmc_port` currently supports `stm32f4` (F42x/43x/469/479, the F4 parts
with an FMC rather than just an FSMC) and `stm32f7`. Both share the exact
same FMC SDRAM controller IP block, so essentially all logic lives in
`src/port/stm32_common/stm32_common.c`, and each family's `src/port/<family>/port.c`
is only a few lines: `dmod_init`/`dmod_deinit` logging, and one
`DMOD_IRQ_HANDLER(STM32_FMC_IRQn)` forwarding to `stm32_fmc_irq_handler()`.

## Steps to add a family whose FMC IP differs (e.g. a hypothetical future family)

1. Create `src/port/<family>/config.cmake`, setting `DMOD_TOOLS_NAME` for the
   target architecture (see `stm32f7/config.cmake` for the pattern).
2. Create `include/port/<family>_regs.h` for anything that genuinely differs
   from `stm32_common_regs.h` (base addresses, IRQ numbers, register bit
   layout if the controller isn't a drop-in match).
3. Decide how much logic can stay shared:
   - If the FMC_SDCRx/SDTRx/SDCMR/SDRTR/SDSR bit layout is identical (true for
     every STM32 family that has an FMC so far), add the family to
     `src/port/CMakeLists.txt`'s `COMMON_SOURCES` selection so it also
     compiles `stm32_common/stm32_common.c`, and write only
     `src/port/<family>/port.c` (lifecycle + IRQ, following the existing
     `stm32f4`/`stm32f7` files).
   - If the register layout genuinely differs, implement the
     `dmod_dmfmc_port_api_declaration(...)` functions from `dmfmc_port.h`
     directly in `src/port/<family>/port.c` instead of relying on
     `stm32_common.c`.
4. Update `CMakeLists.txt`'s `DMFMC_MCU_SERIES` cache variable documentation
   and this repo's CI matrix (bitbucket-pipelines.yml / GitHub workflow) to
   include the new value.
5. Add a `configs/board/<board>/sdram.ini` for at least one board on the new
   family, with every FMC pin's `dmgpio` section plus the `dmfmc` section -
   see `configs/board/stm32f746g-disco/sdram.ini`.

## Notes for whoever validates the STM32F4 port

The STM32F7 port has been checked against a real STM32F746G-Discovery board
and its MT48LC4M32B2 SDRAM. The STM32F4 port (`src/port/stm32f4/`) has
**not** been validated on hardware yet - it shares 100% of its logic with the
F7 port through `stm32_common.c` on the assumption that the FMC SDRAM
controller is register-identical between STM32F7 and the F42x/43x/469/479
parts, which is true per ST's reference manuals but is worth confirming
against the exact silicon revision before shipping. In particular, double
check:
- `STM32_FMC_IRQn` (assumed `48` on both families, in `stm32_common_regs.h`).
- The FMC/RCC base addresses (assumed identical, in `stm32_common_regs.h`).
- Whether the target F4 part actually has an FMC (SDRAM-capable) rather than
  only an FSMC (NOR/SRAM/NAND only, no SDRAM controller) - only 429/439/469/479
  parts have the former.
