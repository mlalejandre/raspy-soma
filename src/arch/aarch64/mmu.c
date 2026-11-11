/* PARCHE 060: MMU y cachés. Mapa de identidad con bloques de 1 GiB (4 KiB de granulo, 48 bits de VA): memoria normal (RAM, cacheable)
 * y memoria de dispositivo (perifericos). Sin MMU todo es memoria de dispositivo: los accesos exclusivos (cerrojos), los accesos no
 * alineados y la velocidad fallan o se arrastran en una placa real. Se llama desde boot.s antes de kernel_main. */
#include <stdint.h>
#include "platform.h"

#define PTE_VALID   1ULL
#define PTE_TABLE   3ULL
#define PTE_AF      (1ULL << 10)
#define PTE_SH_INNER (3ULL << 8)
#define PTE_ATTR(n) ((uint64_t)(n) << 2)
#define PTE_PXN     (1ULL << 53)
#define PTE_UXN     (1ULL << 54)
#define ATTR_DEVICE 0   /* MAIR[7:0]  = 0x00: Device-nGnRnE */
#define ATTR_NORMAL 1   /* MAIR[15:8] = 0xFF: Normal, write-back, read/write allocate */
#define ATTR_NC     2   /* MAIR[23:16] = 0x44: Normal, no cacheable (PARCHE 071: memoria de DMA) */

static uint64_t l0[512] __attribute__((aligned(4096)));
static uint64_t l1[512] __attribute__((aligned(4096)));
#if SOMA_DMA_POOL_SIZE
static uint64_t l2[512] __attribute__((aligned(4096)));      /* primer GiB en bloques de 2 MiB para poder marcar el pool de DMA como no cacheable */
#endif

void mmu_early_init(void)
{
    for (int i = 0; i < 512; i++) { l0[i] = 0; l1[i] = 0; }
    l0[0] = (uint64_t)(uintptr_t)l1 | PTE_TABLE;
    for (int g = SOMA_MMU_DEVICE_GIB_FIRST; g <= SOMA_MMU_DEVICE_GIB_LAST; g++)
        l1[g] = ((uint64_t)g << 30) | PTE_VALID | PTE_AF | PTE_ATTR(ATTR_DEVICE) | PTE_PXN | PTE_UXN;
    for (int g = SOMA_MMU_NORMAL_GIB_FIRST; g <= SOMA_MMU_NORMAL_GIB_LAST; g++)
        l1[g] = ((uint64_t)g << 30) | PTE_VALID | PTE_AF | PTE_SH_INNER | PTE_ATTR(ATTR_NORMAL);
#if SOMA_DMA_POOL_SIZE
    for (int i = 0; i < 512; i++) {
        uint64_t a = (uint64_t)i << 21;
        int dma = a >= (SOMA_DMA_POOL_BASE & ~0x1FFFFFULL) && a < SOMA_DMA_POOL_BASE + SOMA_DMA_POOL_SIZE;
        l2[i] = a | PTE_VALID | PTE_AF | (dma ? (2ULL << 8) | PTE_ATTR(ATTR_NC) : PTE_SH_INNER | PTE_ATTR(ATTR_NORMAL));   /* NC: compartible exterior */
    }
    l1[0] = (uint64_t)(uintptr_t)l2 | PTE_TABLE;
#endif

    uint64_t mmfr0, tcr, sctlr;
    __asm__ volatile ("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    uint64_t ips = mmfr0 & 0xF; if (ips > 5) ips = 5;                       /* hasta 48 bits fisicos */
    tcr = 16ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (1ULL << 23) | (ips << 32);   /* T0SZ=16, WBWA, inner shareable, 4 KiB, TTBR1 desactivado */
    __asm__ volatile ("dsb ish");
    __asm__ volatile ("msr mair_el1, %0" :: "r"(0x44FF00ULL));
    __asm__ volatile ("msr tcr_el1, %0" :: "r"(tcr));
    __asm__ volatile ("msr ttbr0_el1, %0" :: "r"((uint64_t)(uintptr_t)l0));
    __asm__ volatile ("isb");
    __asm__ volatile ("tlbi vmalle1\n dsb nsh\n ic iallu\n dsb nsh\n isb");
    __asm__ volatile ("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);                      /* M, C, I */
    sctlr &= ~(1ULL << 1);                                                  /* A: sin comprobacion de alineacion en memoria normal */
    __asm__ volatile ("msr sctlr_el1, %0\n isb" :: "r"(sctlr) : "memory");
}
