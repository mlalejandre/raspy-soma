/*
 * PARCHE 068 (SOMA): RP1 minimo para probar SOMA sin una Pi 5 fisica.
 * Endpoint PCIe 1de4:0001 con BAR0 (16 KiB, sin uso), BAR1 (4 MiB) y BAR2 (64 KiB, sin uso). En la BAR1: SYSINFO en 0
 * (primer registro = identificador del chip) y un controlador xHCI en 0x200000, como el usbhost0 del RP1. NO modela el resto del
 * RP1 (GPIO, relojes, MSI-X): sirve para ejercitar la deteccion y el xHCI del driver. Los valores salen del datasheet
 * publico y de Linux, no de una placa.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "net/net.h"
#include "hw/net/cadence_gem.h"
#include "../usb/hcd-xhci-sysbus.h"

#define TYPE_SOMA_RP1 "soma-rp1"
OBJECT_DECLARE_SIMPLE_TYPE(SomaRp1State, SOMA_RP1)

#define RP1_BAR1_SIZE   0x400000
#define RP1_XHCI0_OFF   0x200000
#define RP1_IOBANK0_OFF 0x0D0000   /* PARCHE 071: GPIO (IO_BANK0, SYS_RIO0, PADS_BANK0) */
#define RP1_SYSRIO_OFF  0x0E0000
#define RP1_PADS_OFF    0x0F0000
#define RP1_ETH_OFF     0x100000   /* "eth" del datasheet: MAC Cadence GEM; PHY en la direccion MDIO 1 como en la Pi 5 */
#define RP1_CHIP_ID     0x20001927u   /* valor de ejemplo del emulador */

struct SomaRp1State {
    PCIDevice parent_obj;
    MemoryRegion bar0, bar1, bar2, sysinfo, hole, iobank, sysrio, pads;
    uint32_t io_ctrl[28], pad[28], rio_out, rio_oe;
    XHCISysbusState xhci;
    CadenceGEMState gem;
};

static uint64_t sysinfo_read(void *opaque, hwaddr addr, unsigned size)
{
    return addr == 0 ? RP1_CHIP_ID : 0;
}
/* GPIO: IO_BANK0 (STATUS/CTRL por pin), SYS_RIO0 (OUT, OE, IN con alias XOR/SET/CLR) y PADS_BANK0 (IE, OD, pull). Una entrada sin forzar lee 1 con pull-up, 0 en el resto. */
static uint64_t iobank_read(void *o, hwaddr a, unsigned sz)
{
    SomaRp1State *s = o;
    uint32_t pin = a / 8;
    if (pin >= 28 || (a & 3)) return 0;
    return (a & 4) ? s->io_ctrl[pin] : 0;
}
static void iobank_write(void *o, hwaddr a, uint64_t v, unsigned sz)
{
    SomaRp1State *s = o;
    uint32_t pin = a / 8;
    if (pin < 28 && (a & 4)) s->io_ctrl[pin] = (uint32_t)v;
}
static uint64_t pads_read(void *o, hwaddr a, unsigned sz)
{
    SomaRp1State *s = o;
    return (a >= 4 && a < 4 + 28 * 4 && !(a & 3)) ? s->pad[(a - 4) / 4] : 0;
}
static void pads_write(void *o, hwaddr a, uint64_t v, unsigned sz)
{
    SomaRp1State *s = o;
    if (a >= 4 && a < 4 + 28 * 4 && !(a & 3)) s->pad[(a - 4) / 4] = (uint32_t)v & 0xFF;
}
static uint64_t rio_read(void *o, hwaddr a, unsigned sz)
{
    SomaRp1State *s = o;
    switch (a & 0xFFF) {
    case 0x0: return s->rio_out;
    case 0x4: return s->rio_oe;
    case 0x8: {
        uint32_t in = 0;
        for (int i = 0; i < 28; i++) {
            int bit = (s->rio_oe >> i) & 1 ? (s->rio_out >> i) & 1 : ((s->pad[i] & (1u << 3)) ? 1 : 0);
            in |= (uint32_t)bit << i;
        }
        return in;
    }
    }
    return 0;
}
static void rio_write(void *o, hwaddr a, uint64_t v, unsigned sz)
{
    SomaRp1State *s = o;
    uint32_t *r = (a & 0xFFF) == 0 ? &s->rio_out : (a & 0xFFF) == 4 ? &s->rio_oe : NULL;
    if (!r) return;
    v &= 0x0FFFFFFF;
    switch ((a >> 12) & 3) {
    case 0: *r = (uint32_t)v; break;
    case 1: *r ^= (uint32_t)v; break;
    case 2: *r |= (uint32_t)v; break;
    default: *r &= ~(uint32_t)v; break;
    }
}
static void sink_write(void *opaque, hwaddr addr, uint64_t v, unsigned size) { }
static uint64_t zero_read(void *opaque, hwaddr addr, unsigned size) { return 0; }

static const MemoryRegionOps sysinfo_ops = { .read = sysinfo_read, .write = sink_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 } };
static const MemoryRegionOps iobank_ops = { .read = iobank_read, .write = iobank_write, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 4, .max_access_size = 4 } };
static const MemoryRegionOps pads_ops = { .read = pads_read, .write = pads_write, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 4, .max_access_size = 4 } };
static const MemoryRegionOps rio_ops = { .read = rio_read, .write = rio_write, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 4, .max_access_size = 4 } };
static const MemoryRegionOps hole_ops = { .read = zero_read, .write = sink_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 } };

static void soma_rp1_realize(PCIDevice *pdev, Error **errp)
{
    SomaRp1State *s = SOMA_RP1(pdev);

    memory_region_init_io(&s->bar0, OBJECT(s), &hole_ops, s, "rp1-bar0", 0x4000);
    memory_region_init_io(&s->bar2, OBJECT(s), &hole_ops, s, "rp1-bar2", 0x10000);
    memory_region_init(&s->bar1, OBJECT(s), "rp1-bar1", RP1_BAR1_SIZE);
    memory_region_init_io(&s->hole, OBJECT(s), &hole_ops, s, "rp1-unmodelled", RP1_BAR1_SIZE);   /* lo no modelado lee 0 */
    memory_region_add_subregion_overlap(&s->bar1, 0, &s->hole, -1);
    memory_region_init_io(&s->sysinfo, OBJECT(s), &sysinfo_ops, s, "rp1-sysinfo", 0x1000);
    memory_region_add_subregion(&s->bar1, 0, &s->sysinfo);

    for (int i = 0; i < 28; i++) { s->io_ctrl[i] = 0x1F; s->pad[i] = 0x56; }      /* sin funcion; pad como lo deja el reset (entrada, sin pull) */
    memory_region_init_io(&s->iobank, OBJECT(s), &iobank_ops, s, "rp1-io-bank0", 0x4000);
    memory_region_add_subregion(&s->bar1, RP1_IOBANK0_OFF, &s->iobank);
    memory_region_init_io(&s->sysrio, OBJECT(s), &rio_ops, s, "rp1-sys-rio0", 0x4000);
    memory_region_add_subregion(&s->bar1, RP1_SYSRIO_OFF, &s->sysrio);
    memory_region_init_io(&s->pads, OBJECT(s), &pads_ops, s, "rp1-pads-bank0", 0x4000);
    memory_region_add_subregion(&s->bar1, RP1_PADS_OFF, &s->pads);

    object_initialize_child(OBJECT(s), "xhci0", &s->xhci, TYPE_XHCI_SYSBUS);
    qdev_prop_set_uint32(DEVICE(&s->xhci), "slots", 64);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xhci), errp)) {
        return;
    }
    s->xhci.xhci.as = pci_get_address_space(pdev);        /* DMA por el bus PCIe (ventana de entrada de PCIe2) */
    memory_region_add_subregion(&s->bar1, RP1_XHCI0_OFF, &s->xhci.xhci.mem);

    /* PARCHE 069: la Ethernet (MAC Cadence GEM) en 0x100000; DMA por el bus PCIe; sin interrupciones (SOMA sondea). Usa el primer -nic. */
    object_initialize_child(OBJECT(s), "eth", &s->gem, TYPE_CADENCE_GEM);
    object_property_set_link(OBJECT(&s->gem), "dma", OBJECT(pci_get_address_space(pdev)->root), &error_abort);
    qdev_prop_set_uint8(DEVICE(&s->gem), "phy-addr", 1);
    qemu_configure_nic_device(DEVICE(&s->gem), true, NULL);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gem), errp)) {
        return;
    }
    memory_region_add_subregion(&s->bar1, RP1_ETH_OFF, sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gem), 0));

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32, &s->bar0);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32, &s->bar1);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_32, &s->bar2);
}

static void soma_rp1_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = soma_rp1_realize;
    k->vendor_id = 0x1de4;
    k->device_id = 0x0001;
    k->revision = 0;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "RP1 minimo de la Raspberry Pi 5 (SYSINFO + xHCI0)";
}

static const TypeInfo soma_rp1_info = {
    .name = TYPE_SOMA_RP1,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SomaRp1State),
    .class_init = soma_rp1_class_init,
    .interfaces = (const InterfaceInfo[]) { { INTERFACE_PCIE_DEVICE }, { } },
};

static void soma_rp1_register_types(void) { type_register_static(&soma_rp1_info); }
type_init(soma_rp1_register_types)
