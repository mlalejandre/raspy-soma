#ifndef SOMA_PCIE_H
#define SOMA_PCIE_H
#include <stdint.h>
/* PARCHE 062: root complex PCIe de la BCM2712 (PCIe1, el conector externo; en la Pi 5 real el RP1 cuelga de PCIe2) y enumeracion. */
#define PCIE_MAX_DEVS 16
struct pcie_bar { uint64_t addr, size; uint8_t is64, prefetch; };
struct pcie_dev {
    uint8_t bus, devfn, hdr, rsvd;
    uint16_t vid, did;
    uint32_t cls;                                   /* clase<<16 | subclase<<8 | interfaz */
    struct pcie_bar bar[6];
};
int pcie_init(void);                                /* 0 = enlace activo y bus enumerado; <0 = no hay root complex, enlace caido o fallo */
int pcie_init_rc(int rc);                            /* PARCHE 067: 0 = PCIe1 (conector), 1 = PCIe2 (RP1); no cambia el seleccionado */
void pcie_select(int rc);                           /* root complex sobre el que actuan pcie_cfg_*, pcie_dev_* (por defecto 0) */
int pcie_present(void);                             /* hay root complex en esta plataforma */
int pcie_dev_count(void);
const struct pcie_dev *pcie_dev_get(int i);
void pcie_format(char *out, uint32_t max);          /* texto para el comando "pci" */
uint32_t pcie_cfg_read32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off);   /* 0xFFFFFFFF si no hay dispositivo */
void pcie_cfg_write32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off, uint32_t v);
uint64_t pcie_dma_offset(void);                     /* direccion PCI = direccion fisica + este valor */
#endif
