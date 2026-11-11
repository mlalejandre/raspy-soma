#include "rp1_proto.h"
/* PARCHE 067 */
static const struct { const char *name; uint32_t addr; uint32_t len; } T[RP1_PERIPH_COUNT] = {
    [RP1_SYSINFO]      = { "sysinfo",      0x40000000u, 0x1000 },
    [RP1_CLOCKS_MAIN]  = { "clocks_main",  0x40018000u, 0x4000 },
    [RP1_CLOCKS_VIDEO] = { "clocks_video", 0x4001C000u, 0x4000 },
    [RP1_ETH]          = { "eth",          0x40100000u, 0x4000 },
    [RP1_ETH_CFG]      = { "eth_cfg",      0x40104000u, 0x4000 },
    [RP1_PCIE_CFG]     = { "pcie",         0x40108000u, 0x8000 },
    [RP1_USB0_CFG]     = { "usbhost0_cfg", 0x40160000u, 0x4000 },
    [RP1_USB1_CFG]     = { "usbhost1_cfg", 0x40164000u, 0x4000 },
    [RP1_DMA]          = { "dma",          0x40188000u, 0x4000 },
    [RP1_IO_BANK0]     = { "io_bank0",     0x400D0000u, 0x4000 },
    [RP1_SYS_RIO0]     = { "sys_rio0",     0x400E0000u, 0x4000 },
    [RP1_PADS_BANK0]   = { "pads_bank0",   0x400F0000u, 0x4000 },
    [RP1_USB0]         = { "usbhost0",     0x40200000u, 0x100000 },
    [RP1_USB1]         = { "usbhost1",     0x40300000u, 0x100000 },
};
int rp1_is_rp1(uint16_t vid, uint16_t did) { return vid == RP1_VID && did == RP1_DID_C0; }
int64_t rp1_periph_off(int id) { return id >= 0 && id < RP1_PERIPH_COUNT ? (int64_t)(T[id].addr - 0x40000000u) : -1; }
uint64_t rp1_periph_len(int id) { return id >= 0 && id < RP1_PERIPH_COUNT ? T[id].len : 0; }
const char *rp1_periph_name(int id) { return id >= 0 && id < RP1_PERIPH_COUNT ? T[id].name : "?"; }
uint64_t rp1_periph_cpu(uint64_t bar1_cpu, uint64_t bar1_size, int id)
{
    int64_t o = rp1_periph_off(id);
    if (o < 0 || !bar1_cpu) return 0;
    if ((uint64_t)o + rp1_periph_len(id) > bar1_size) return 0;
    return bar1_cpu + (uint64_t)o;
}
