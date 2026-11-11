#include "pcie_proto.h"

uint64_t pcie_align_up(uint64_t v, uint64_t a) { return a ? (v + a - 1) & ~(a - 1) : v; }

uint64_t pcie_bar_size(uint32_t lo, uint32_t hi, int *is64, int *prefetch)
{
    int w = 0, p = 0;
    if (is64) *is64 = 0;
    if (prefetch) *prefetch = 0;
    if (lo & 1u) return 0;                                      /* E/S: la Pi 5 no tiene espacio de E/S */
    w = ((lo >> 1) & 3u) == 2u; p = (lo >> 3) & 1u;
    if (((lo >> 1) & 3u) == 1u || ((lo >> 1) & 3u) == 3u) return 0;
    uint64_t mask = lo & 0xFFFFFFF0u;
    if (w) mask |= (uint64_t)hi << 32; else mask |= 0xFFFFFFFF00000000ULL;
    if (!(lo & 0xFFFFFFF0u) && !(w && hi)) return 0;
    uint64_t size = ~mask + 1;
    if (size == 0 || (size & (size - 1))) return 0;
    if (is64) *is64 = w;
    if (prefetch) *prefetch = p;
    return size;
}

int pcie_alloc_bar(struct pcie_alloc *a, uint64_t size, uint64_t *addr)
{
    if (!size || (size & (size - 1))) return -1;
    uint64_t at = pcie_align_up(a->next, size);
    if (at < a->next || at + size < at || at + size > a->limit) return -1;
    *addr = at; a->next = at + size;
    return 0;
}

int pcie_outwin_regs(uint64_t cpu_base, uint64_t pci_base, uint64_t size, struct pcie_outwin *o)
{
    const uint64_t MB = 1ULL << 20;
    if (!size || (cpu_base % MB) || (size % MB)) return -1;
    uint64_t b = cpu_base / MB, l = (cpu_base + size) / MB - 1;
    if (b >= (1ULL << 20) || l >= (1ULL << 20)) return -1;
    o->lo = (uint32_t)pci_base; o->hi = (uint32_t)(pci_base >> 32);
    o->base_limit = (uint32_t)((b & 0xFFF) << 4) | (uint32_t)((l & 0xFFF) << 20);
    o->base_hi = (uint32_t)((b >> 12) & 0xFF); o->limit_hi = (uint32_t)((l >> 12) & 0xFF);
    return 0;
}

int pcie_inbar_regs(uint64_t pci_base, uint64_t size, uint64_t cpu_base, uint32_t cfg[2], uint32_t remap[2])
{
    unsigned code = 0;
    if (size < (64u << 10) || size > (1ULL << 36) || (size & (size - 1))) return -1;
    while ((1ULL << (code + 15)) < size) code++;               /* tamano = 2^(code+15) */
    if ((pci_base & (size - 1)) || (cpu_base & 0xFFF)) return -1;
    cfg[0] = (uint32_t)pci_base | code; cfg[1] = (uint32_t)(pci_base >> 32);
    remap[0] = (uint32_t)cpu_base | 1u; remap[1] = (uint32_t)(cpu_base >> 32);   /* bit 0 = ACCESS_EN */
    return 0;
}

uint32_t pcie_ecfg_index(uint8_t bus, uint8_t devfn) { return ((uint32_t)bus << 20) | ((uint32_t)devfn << 12); }

const char *pcie_class_name(uint32_t c)
{
    switch (c >> 8) {
    case 0x0604: return "puente PCI-PCI";
    case 0x0c03: return c == 0x0c0330 ? "controlador USB xHCI" : "controlador USB";
    case 0x0108: return "controlador NVMe";
    case 0x0106: return "controlador SATA";
    case 0x0200: return "Ethernet";
    case 0x0300: return "VGA";
    case 0x0403: return "audio";
    case 0x0600: return "puente host";
    default: break;
    }
    return (c >> 16) == 0x01 ? "almacenamiento" : (c >> 16) == 0x02 ? "red" : (c >> 16) == 0x06 ? "puente" : "otro";
}
