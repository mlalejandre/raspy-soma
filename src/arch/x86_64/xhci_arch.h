#ifndef SOMA_XHCI_ARCH_H
#define SOMA_XHCI_ARCH_H
/* PARCHE 062: lo que xhci.c necesita de cada arquitectura (x86_64). */
#include <vmm.h>
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#define XHCI_RELAX()          __asm__ volatile ("pause")
#define XHCI_MB()             __asm__ volatile ("" ::: "memory")
#define XHCI_BUS(p)           ((uint64_t)(p))                      /* direccion fisica -> direccion que ve el controlador (identidad) */
#define XHCI_PCI_BUSES        256
#define XHCI_BAR_OK(bar)      ((bar) >= 0x40000000ULL)             /* fuera del mapa de identidad de 1 GiB */
#define XHCI_BAR_CPU(bar)     (bar)
static inline int xhci_arch_init(void) { return 0; }
#define XHCI_DMA_ALLOC(n) pmm_alloc_contig(n)
static inline int xhci_arch_alt_bar(uint64_t *bar) { (void)bar; return -1; }     /* PARCHE 067: sin controlador fuera de PCI en x86 */
static inline int xhci_map_mmio(uint64_t bar)
{
    for (uint64_t off = 0; off < 0x10000; off += 0x1000)
        if (vmm_map_page(bar + off, bar + off, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | PTE_PWT | PTE_PCD) != 0) return -1;
    return 0;
}
static inline unsigned long irq_save(void) { unsigned long f; __asm__ volatile ("pushfq; cli; popq %0" : "=r"(f) :: "memory"); return f; }
static inline void irq_restore(unsigned long f) { __asm__ volatile ("pushq %0; popfq" :: "r"(f) : "memory", "cc"); }
#endif
