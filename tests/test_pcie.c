#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/pcie_proto.h"
int main(void)
{
    int w, p;
    assert(pcie_align_up(0x1001, 0x1000) == 0x2000 && pcie_align_up(5, 0) == 5);
    /* BAR de 16 KiB, 64 bits, sin prefetch: lo = 0xFFFFC004, hi = 0xFFFFFFFF */
    assert(pcie_bar_size(0xFFFFC004u, 0xFFFFFFFFu, &w, &p) == 0x4000 && w == 1 && p == 0);
    assert(pcie_bar_size(0xFFFFF000u, 0, &w, &p) == 0x1000 && w == 0);               /* 32 bits, 4 KiB */
    assert(pcie_bar_size(0xFFFFF008u, 0, &w, &p) == 0x1000 && p == 1);
    assert(pcie_bar_size(0xFFFFFFF1u, 0, &w, &p) == 0);                              /* E/S */
    assert(pcie_bar_size(0, 0, &w, &p) == 0);                                        /* no existe */
    assert(pcie_bar_size(0x00000004u, 0xFFFFFFF0u, &w, &p) == (1ULL << 36) && w == 1);   /* 64 GiB */
    assert(pcie_bar_size(0xFFFFD000u, 0, &w, &p) == 0);                              /* no potencia de 2 (mascara con hueco) */
    struct pcie_alloc a = { 0x100000, 0x40000000 };
    uint64_t x;
    assert(pcie_alloc_bar(&a, 0x4000, &x) == 0 && x == 0x100000);
    assert(pcie_alloc_bar(&a, 0x1000, &x) == 0 && x == 0x104000);
    assert(pcie_alloc_bar(&a, 0x10000, &x) == 0 && x == 0x110000);
    assert(pcie_alloc_bar(&a, 0x80000000ULL, &x) == -1);
    assert(pcie_alloc_bar(&a, 3, &x) == -1 && pcie_alloc_bar(&a, 0, &x) == -1);
    struct pcie_outwin o;
    assert(pcie_outwin_regs(0x1800000000ULL, 0, 1ULL << 30, &o) == 0);
    assert(o.base_limit == (0u << 4 | (0x3FFu << 20)) && o.base_hi == 0x18 && o.limit_hi == 0x18 && o.lo == 0 && o.hi == 0);
    assert(pcie_outwin_regs(0x1800000001ULL, 0, 1 << 20, &o) == -1 && pcie_outwin_regs(0x1800000000ULL, 0, 0, &o) == -1);
    uint32_t c[2], r[2];
    assert(pcie_inbar_regs(0x1000000000ULL, 1ULL << 36, 0, c, r) == 0 && c[0] == 21 && c[1] == 0x10 && r[0] == 1 && r[1] == 0);
    assert(pcie_inbar_regs(0, 1ULL << 16, 0x1000, c, r) == 0 && c[0] == 1 && r[0] == 0x1001);
    assert(pcie_inbar_regs(0x1000, 1ULL << 16, 0, c, r) == -1 && pcie_inbar_regs(0, 100, 0, c, r) == -1);
    assert(pcie_ecfg_index(1, 0x08) == 0x00108000u);
    assert(!strcmp(pcie_class_name(0x0c0330), "controlador USB xHCI") && !strcmp(pcie_class_name(0x060400), "puente PCI-PCI"));
    puts("pcie_proto: todo correcto");
    return 0;
}
