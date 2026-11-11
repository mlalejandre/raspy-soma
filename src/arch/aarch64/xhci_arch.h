#ifndef SOMA_XHCI_ARCH_H
#define SOMA_XHCI_ARCH_H
/* PARCHE 062: lo que xhci.c necesita en aarch64. La MMU de SOMA mapea los perifericos como Device en bloques de 1 GiB: no hay que
 * mapear el BAR. Los dispositivos ven la RAM en SOMA_PCIE_DMA_PCI + direccion fisica. */
#include "platform.h"
#include "pcie.h"
#include "rp1.h"
#define XHCI_RELAX()          __asm__ volatile ("yield")
#define XHCI_MB()             __asm__ volatile ("dsb sy" ::: "memory")
#define XHCI_BUS(p)           ((uint64_t)(p) + SOMA_PCIE_DMA_PCI)
#ifdef XHCI_SECOND
#define XHCI_PCI_BUSES        0                                    /* PARCHE 077: el segundo controlador es siempre el usbhost1 del RP1 */
#define XHCI_RP1_INDEX        1
#else
#define XHCI_PCI_BUSES        (SOMA_PCIE_BASE ? 8 : 0)             /* en QEMU virt (ECAM) no se busca */
#define XHCI_RP1_INDEX        0
#endif
#define XHCI_BAR_OK(bar)      ((bar) != 0)
#define XHCI_BAR_CPU(bar)     ((bar) + SOMA_PCIE_MEM_CPU)               /* la ventana de salida empieza en PCI 0 */
static inline int xhci_arch_init(void) { return SOMA_PCIE_BASE ? pcie_init() : -1; }
/* PARCHE 067: si no hay xHCI como dispositivo PCI, el de la Pi 5 real cuelga del RP1 (memoria, no PCI) */
#define XHCI_DMA_ALLOC(n) pmm_alloc_dma(n)          /* PARCHE 071: pool de DMA no cacheable */
static inline int xhci_arch_alt_bar(uint64_t *bar) { if (rp1_init() != 0) return -1; uint64_t b = rp1_xhci_base(XHCI_RP1_INDEX); if (!b) return -1; *bar = b; return 0; }
static inline int xhci_map_mmio(uint64_t bar) { (void)bar; return 0; }
static inline unsigned long irq_save(void) { unsigned long f; __asm__ volatile ("mrs %0, daif\n msr daifset, #2" : "=r"(f) :: "memory"); return f; }
static inline void irq_restore(unsigned long f) { __asm__ volatile ("msr daif, %0" :: "r"(f) : "memory"); }
#endif
