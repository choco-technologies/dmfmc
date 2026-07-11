#ifndef DMFMC_STM32_COMMON_REGS_H
#define DMFMC_STM32_COMMON_REGS_H

#include <stdint.h>

/* ======================================================================
 *   Common STM32 FMC (Bank 5/6, SDRAM controller) Register Definitions
 *
 *   The FMC SDRAM controller IP block is identical on every STM32F4 part
 *   that has an FMC (429/439/469/479 - as opposed to the smaller parts,
 *   which only have the FSMC without SDRAM support) and on STM32F7. Bit
 *   layout below is taken from the FMC_SDCRx/SDTRx/SDCMR/SDRTR/SDSR
 *   description shared by both families' reference manuals - re-check
 *   against the exact part's reference manual before relying on it on
 *   new silicon.
 * ====================================================================== */

typedef struct
{
    volatile uint32_t SDCR[2];  /**< 0x140/0x144 - SDRAM Control registers (bank1/bank2) */
    volatile uint32_t SDTR[2];  /**< 0x148/0x14C - SDRAM Timing registers (bank1/bank2) */
    volatile uint32_t SDCMR;    /**< 0x150 - SDRAM Command mode register */
    volatile uint32_t SDRTR;    /**< 0x154 - SDRAM Refresh timer register */
    volatile uint32_t SDSR;     /**< 0x158 - SDRAM Status register */
} FMC_Bank5_6_TypeDef;

#define STM32_FMC_BANK5_6_BASE      0xA0000140U

/* FMC_SDCRx bits (bank-specific fields apply to both SDCR1/SDCR2; SDCLK,
 * RBURST and RPIPE are shared between banks and only take effect from SDCR1) */
#define FMC_SDCR_NC_Pos             0U      /**< Column address bits: 0=8,1=9,2=10,3=11 */
#define FMC_SDCR_NC_Msk             (0x3U << FMC_SDCR_NC_Pos)
#define FMC_SDCR_NR_Pos             2U      /**< Row address bits: 0=11,1=12,2=13 */
#define FMC_SDCR_NR_Msk             (0x3U << FMC_SDCR_NR_Pos)
#define FMC_SDCR_MWID_Pos           4U      /**< Data bus width: 0=8bit,1=16bit,2=32bit */
#define FMC_SDCR_MWID_Msk           (0x3U << FMC_SDCR_MWID_Pos)
#define FMC_SDCR_NB                 (1U << 6)  /**< Number of internal banks: 0=2, 1=4 */
#define FMC_SDCR_CAS_Pos            7U      /**< CAS latency in cycles: 1,2,3 */
#define FMC_SDCR_CAS_Msk            (0x3U << FMC_SDCR_CAS_Pos)
#define FMC_SDCR_WP                 (1U << 9)  /**< Write protection */
#define FMC_SDCR_SDCLK_Pos          10U     /**< SDCLK divider (SDCR1 only): 0/1=disabled,2=HCLK/2,3=HCLK/3 */
#define FMC_SDCR_SDCLK_Msk          (0x3U << FMC_SDCR_SDCLK_Pos)
#define FMC_SDCR_RBURST             (1U << 12) /**< Burst read (SDCR1 only) */
#define FMC_SDCR_RPIPE_Pos          13U     /**< Read pipe delay in cycles (SDCR1 only) */
#define FMC_SDCR_RPIPE_Msk          (0x3U << FMC_SDCR_RPIPE_Pos)

/* FMC_SDTRx bits (all delays encoded as cycles-1; TRC and TRP are shared
 * between banks and only take effect from SDTR1) */
#define FMC_SDTR_TMRD_Pos           0U      /**< Load-mode-register to Active/Refresh delay */
#define FMC_SDTR_TMRD_Msk           (0xFU << FMC_SDTR_TMRD_Pos)
#define FMC_SDTR_TXSR_Pos           4U      /**< Exit self-refresh delay */
#define FMC_SDTR_TXSR_Msk           (0xFU << FMC_SDTR_TXSR_Pos)
#define FMC_SDTR_TRAS_Pos           8U      /**< Minimum self-refresh period */
#define FMC_SDTR_TRAS_Msk           (0xFU << FMC_SDTR_TRAS_Pos)
#define FMC_SDTR_TRC_Pos            12U     /**< Row cycle delay (SDTR1 only) */
#define FMC_SDTR_TRC_Msk            (0xFU << FMC_SDTR_TRC_Pos)
#define FMC_SDTR_TWR_Pos            16U     /**< Write recovery delay */
#define FMC_SDTR_TWR_Msk            (0xFU << FMC_SDTR_TWR_Pos)
#define FMC_SDTR_TRP_Pos            20U     /**< Row precharge delay (SDTR1 only) */
#define FMC_SDTR_TRP_Msk            (0xFU << FMC_SDTR_TRP_Pos)
#define FMC_SDTR_TRCD_Pos           24U     /**< Row-to-column delay */
#define FMC_SDTR_TRCD_Msk           (0xFU << FMC_SDTR_TRCD_Pos)

/* FMC_SDCMR bits */
#define FMC_SDCMR_MODE_Pos          0U
#define FMC_SDCMR_MODE_Msk          (0x7U << FMC_SDCMR_MODE_Pos)
#define FMC_SDCMR_MODE_NORMAL       0x0U
#define FMC_SDCMR_MODE_CLK_ENABLE   0x1U
#define FMC_SDCMR_MODE_PALL         0x2U
#define FMC_SDCMR_MODE_AUTOREFRESH  0x3U
#define FMC_SDCMR_MODE_LOADMODEREG  0x4U
#define FMC_SDCMR_MODE_SELFREFRESH  0x5U
#define FMC_SDCMR_MODE_POWERDOWN    0x6U
#define FMC_SDCMR_CTB1              (1U << 3)  /**< Command target: bank 1 */
#define FMC_SDCMR_CTB2              (1U << 4)  /**< Command target: bank 2 */
#define FMC_SDCMR_NRFS_Pos          5U         /**< Number of auto-refresh cycles - 1 */
#define FMC_SDCMR_NRFS_Msk          (0xFU << FMC_SDCMR_NRFS_Pos)
#define FMC_SDCMR_MRD_Pos           9U         /**< Mode register value for LOAD MODE REGISTER command */
#define FMC_SDCMR_MRD_Msk           (0x1FFFU << FMC_SDCMR_MRD_Pos)

/* FMC_SDRTR bits */
#define FMC_SDRTR_CRE               (1U << 0)  /**< Clear refresh error flag */
#define FMC_SDRTR_COUNT_Pos         1U         /**< Refresh counter, in SDCLK cycles */
#define FMC_SDRTR_COUNT_Msk         (0x1FFFU << FMC_SDRTR_COUNT_Pos)
#define FMC_SDRTR_REIE              (1U << 14) /**< Refresh error interrupt enable */

/* FMC_SDSR bits */
#define FMC_SDSR_RE                 (1U << 0)  /**< Refresh error flag */
#define FMC_SDSR_MODES1_Pos         1U
#define FMC_SDSR_MODES1_Msk         (0x3U << FMC_SDSR_MODES1_Pos)
#define FMC_SDSR_MODES2_Pos         3U
#define FMC_SDSR_MODES2_Msk         (0x3U << FMC_SDSR_MODES2_Pos)
#define FMC_SDSR_BUSY               (1U << 5)  /**< Controller is busy executing a command */

/* SDRAM memory windows (identical on STM32F4 parts with FMC and STM32F7) */
#define STM32_FMC_SDRAM_BANK1_BASE  0xC0000000U
#define STM32_FMC_SDRAM_BANK2_BASE  0xD0000000U

/* RCC register structure (subset needed to enable the FMC clock) */
typedef struct
{
    volatile uint32_t CR;
    volatile uint32_t PLLCFGR;
    volatile uint32_t CFGR;
    volatile uint32_t CIR;
    volatile uint32_t AHB1RSTR;
    volatile uint32_t AHB2RSTR;
    volatile uint32_t AHB3RSTR;
    volatile uint32_t RESERVED0;
    volatile uint32_t APB1RSTR;
    volatile uint32_t APB2RSTR;
    volatile uint32_t RESERVED1[2];
    volatile uint32_t AHB1ENR;
    volatile uint32_t AHB2ENR;
    volatile uint32_t AHB3ENR;
} FMC_RCC_TypeDef;

#define RCC_AHB3ENR_FMCEN           (1U << 0)

/* NVIC Interrupt Set/Clear-Enable registers (ARMv7-M, common to Cortex-M4/M7) */
#define NVIC_ISER                   ((volatile uint32_t *)0xE000E100UL)
#define NVIC_ICER                   ((volatile uint32_t *)0xE000E180UL)

/* Base addresses: identical on every STM32F4 part with an FMC (429/439/469/479)
 * and on STM32F7, so they live here instead of being duplicated per family. */
#define STM32_FMC_RCC_BASE          0x40023800U
#define STM32_FMC_BASE              0xA0000000U

/* FMC global interrupt (shared by SDRAM refresh error, NAND and PCCARD) */
#define STM32_FMC_IRQn              48

#endif // DMFMC_STM32_COMMON_REGS_H
