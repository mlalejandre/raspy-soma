#ifndef SOMA_HWINFO_H
#define SOMA_HWINFO_H
/* PARCHE 032: descripcion del hardware (solo lectura). Analizadores puros de las tablas ACPI (x86) y del arbol de
 * dispositivos FDT/DTB (Raspberry Pi y otras ARM). No tocan el hardware: reciben una funcion `map` que devuelve un
 * puntero a memoria fisica ya mapeada (o 0 si no es accesible), asi que se prueban y se fuzzean en el host.
 * Son la base para escribir despues los controladores (APIC/MCFG/xHCI/AHCI/NVMe en x86; UART/GIC/timer en ARM). */
#include <stdint.h>
#include <stddef.h>

typedef const uint8_t *(*hw_map_fn)(uint64_t phys, size_t len, void *ctx);

#define HW_MAX_IOAPIC 8
#define HW_MAX_MCFG   4

struct hw_acpi {
    int      ok;                    /* 1 = RSDP valido y al menos una tabla leida */
    uint8_t  revision;              /* revision del RSDP (0 = ACPI 1.0, >=2 usa XSDT) */
    char     oem[7];
    int      ntables;               /* tablas listadas por RSDT/XSDT */
    char     sig[32][5];            /* firmas de las primeras 32 tablas */
    int      ncpu;                  /* procesadores habilitados segun la MADT */
    uint32_t lapic_addr;
    int      nioapic;
    uint32_t ioapic_addr[HW_MAX_IOAPIC];
    int      nmcfg;
    struct { uint64_t base; uint16_t seg; uint8_t bus0, bus1; } mcfg[HW_MAX_MCFG];
    int      has_hpet, has_fadt, has_madt;
    int      nirq_ovr;              /* PARCHE 034: overrides de IRQ ISA -> GSI (MADT tipo 2) */
    struct { uint8_t src; uint32_t gsi; uint16_t flags; } irq_ovr[16];
};
/* Analiza desde la direccion fisica del RSDP. Devuelve 0 si lo logro. */
int hw_acpi_parse(uint64_t rsdp_phys, hw_map_fn map, void *ctx, struct hw_acpi *o);
/* Busca "RSD PTR " (alineado a 16) en [lo, hi). Devuelve la direccion fisica o 0. */
uint64_t hw_acpi_find_rsdp(uint64_t lo, uint64_t hi, hw_map_fn map, void *ctx);

/* PARCHE 034: capacidades vendor de virtio-pci (virtio 1.x) a partir de una copia de 256 bytes del espacio de configuracion */
#define HW_VIRTIO_COMMON 1
#define HW_VIRTIO_NOTIFY 2
#define HW_VIRTIO_ISR    3
#define HW_VIRTIO_DEVICE 4
#define HW_VIRTIO_PCICFG 5
struct hw_virtio_cap { uint8_t type, bar; uint32_t offset, length, notify_mult; };
int hw_virtio_caps(const uint8_t cfg[256], struct hw_virtio_cap *out, int max);   /* devuelve cuantas encontro */

#define HW_FDT_MAX_MEM 4
struct hw_fdt {
    int      ok;
    uint32_t totalsize, version;
    char     model[64], compat[64], stdout_path[64];
    int      ncpu, nnodes, nmem;
    uint64_t mem_base[HW_FDT_MAX_MEM], mem_size[HW_FDT_MAX_MEM];
};
int hw_fdt_parse(const uint8_t *fdt, size_t avail, struct hw_fdt *o);
/* Primer nodo cuyo "compatible" contiene `compat`: devuelve 0 y su primer par reg (base, tam) si lo tiene. */
int hw_fdt_find_reg(const uint8_t *fdt, size_t avail, const char *compat, uint64_t *base, uint64_t *size);
#endif
