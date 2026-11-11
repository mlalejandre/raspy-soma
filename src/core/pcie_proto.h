#ifndef SOMA_PCIE_PROTO_H
#define SOMA_PCIE_PROTO_H
#include <stdint.h>
/* PARCHE 062: logica pura de PCIe (probada en el host): tamano de BAR, reparto de ventanas, registros de ventana del root complex
 * Broadcom STB (BCM2712), nombres de clase. */
struct pcie_alloc { uint64_t next, limit; };                       /* reparto de PCI memoria: next = primera direccion libre, limit = fin (exclusivo) */
uint64_t pcie_align_up(uint64_t v, uint64_t a);                    /* a potencia de 2; a == 0 devuelve v */
/* Tamano de un BAR de memoria a partir de las lecturas tras escribir todo unos (lo/hi; hi se ignora si no es de 64 bits).
 * Devuelve 0 si el BAR no existe, es de E/S o su tamano no es una potencia de 2. is64/prefetch se rellenan si no son NULL. */
uint64_t pcie_bar_size(uint32_t lo, uint32_t hi, int *is64, int *prefetch);
int pcie_alloc_bar(struct pcie_alloc *a, uint64_t size, uint64_t *addr);   /* alineado a su tamano; 0 = bien, -1 = no cabe */
/* Registros de la ventana de salida n del root complex: CPU [cpu_base, cpu_base+size) -> PCI pci_base. Devuelve 0 si no son validos
 * (tamano o base no multiplos de 1 MiB, o size == 0). */
struct pcie_outwin { uint32_t lo, hi, base_limit, base_hi, limit_hi; };
int pcie_outwin_regs(uint64_t cpu_base, uint64_t pci_base, uint64_t size, struct pcie_outwin *o);
/* Registros de un RC_BAR de entrada: PCI [pci_base, +size) -> CPU cpu_base. size potencia de 2 entre 64 KiB y 64 GiB. 0 = bien. */
int pcie_inbar_regs(uint64_t pci_base, uint64_t size, uint64_t cpu_base, uint32_t cfg[2], uint32_t remap[2]);
uint32_t pcie_ecfg_index(uint8_t bus, uint8_t devfn);              /* registro EXT_CFG_INDEX */
const char *pcie_class_name(uint32_t class24);                     /* clase<<16|subclase<<8|interfaz */
#endif
