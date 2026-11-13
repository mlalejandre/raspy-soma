/* PARCHE 089: datos de hardware en aarch64 para el Monitor del sistema. MIDR_EL1 (fabricante y nucleo) y, en la Raspberry Pi 5, el
 * firmware por el buzon: modelo y revision de la placa, numero de serie, temperatura del SoC, frecuencias del ARM y de la GPU
 * VideoCore VII (nucleo y V3D), tension y reparto de memoria ARM/GPU. */
#include "hwmon.h"
#include "smp.h"
#include "mem.h"
#include "ramstat.h"
#include "sysinfo.h"
#include "rpi_fb.h"
#include "sdio_proto.h"

static void cat(char *d, const char *s, int max) { int i = 0; while (d[i]) i++; while (*s && i + 1 < max) d[i++] = *s++; d[i] = 0; }
#ifdef PLATFORM_rpi5
static void catn(char *d, uint32_t v, int max) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; cat(d, r, max); }
#endif

#ifdef PLATFORM_rpi5
/* una etiqueta del buzon con hasta 2 palabras de entrada; devuelve 0 y la respuesta en v[0..1] */
static int mbox1(uint32_t tag, uint32_t in0, uint32_t *v0, uint32_t *v1)
{
    static uint32_t __attribute__((aligned(16))) b[10];
    b[0] = 10 * 4; b[1] = 0; b[2] = tag; b[3] = 8; b[4] = 0; b[5] = in0; b[6] = 0; b[7] = 0; b[8] = 0; b[9] = 0;
    if (rpi_mbox_prop(b, sizeof b) != 0 || b[1] != 0x80000000u) return -1;
    if (v0) *v0 = b[5];
    if (v1) *v1 = b[6];
    return 0;
}
static uint32_t clock_mhz(uint32_t id, int max) { uint32_t a, r; if (mbox1(max ? 0x00030004u : 0x00030002u, id, &a, &r) || a != id) return 0; return r / 1000000u; }
#endif

int hwmon_live(int *temp_mc, uint32_t *mhz)
{
    *temp_mc = -1; *mhz = 0;
#ifdef PLATFORM_rpi5
    uint32_t a, v;
    if (mbox1(0x00030006u, 0, &a, &v) == 0) *temp_mc = (int)v;
    *mhz = clock_mhz(3, 0);
    return 0;
#else
    return -1;
#endif
}

int hwmon_cpu(struct hw_cpu *o)
{
    memset(o, 0, sizeof *o);
    uint64_t midr; __asm__ volatile ("mrs %0, midr_el1" : "=r"(midr));
    uint32_t impl = (uint32_t)(midr >> 24) & 255, part = (uint32_t)(midr >> 4) & 0xFFF, var = (uint32_t)(midr >> 20) & 15, rev = (uint32_t)midr & 15;
    cat(o->arch, "AArch64 (ARMv8)", sizeof o->arch);
    cat(o->vendor, impl == 0x41 ? "ARM" : impl == 0x61 ? "Apple" : impl == 0x51 ? "Qualcomm" : impl == 0x48 ? "HiSilicon" : "?", sizeof o->vendor);
    const char *nm = part == 0xD0B ? "Cortex-A76" : part == 0xD08 ? "Cortex-A72" : part == 0xD03 ? "Cortex-A53" : part == 0xD07 ? "Cortex-A57" :
                     part == 0xD05 ? "Cortex-A55" : part == 0xD0C ? "Neoverse N1" : part == 0xD40 ? "Neoverse V1" : part == 0xD41 ? "Cortex-A78" : part == 0xD44 ? "Cortex-X1" : 0;
    if (nm) { cat(o->brand, o->vendor, sizeof o->brand); cat(o->brand, " ", sizeof o->brand); cat(o->brand, nm, sizeof o->brand); }
    else { cat(o->brand, "Nucleo ARM 0x", sizeof o->brand); const char *hx = "0123456789ABCDEF"; char t[4] = { hx[(part >> 8) & 15], hx[(part >> 4) & 15], hx[part & 15], 0 }; cat(o->brand, t, sizeof o->brand); }
    o->family = 8; o->model = (int)part; o->stepping = (int)(var * 16 + rev);
    o->cores = smp_cpus(); o->threads = o->cores;
    uint64_t isar0; __asm__ volatile ("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
    uint64_t pfr0; __asm__ volatile ("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
    if (((pfr0 >> 16) & 15) != 15) cat(o->feats, "FP ", sizeof o->feats);
    if (((pfr0 >> 20) & 15) != 15) cat(o->feats, "NEON ", sizeof o->feats);
    if ((isar0 >> 4) & 15) cat(o->feats, "AES ", sizeof o->feats);
    if ((isar0 >> 8) & 15) cat(o->feats, "SHA1 ", sizeof o->feats);
    if ((isar0 >> 12) & 15) cat(o->feats, "SHA2 ", sizeof o->feats);
    if ((isar0 >> 16) & 15) cat(o->feats, "CRC32 ", sizeof o->feats);
    if ((isar0 >> 20) & 15) cat(o->feats, "LSE ", sizeof o->feats);
    if ((isar0 >> 44) & 15) cat(o->feats, "DotProd ", sizeof o->feats);
    if (part == 0xD0B) { o->l1d_kb = 64; o->l1i_kb = 64; o->l2_kb = 512; o->l3_kb = 2048; }       /* BCM2712 */
    else if (part == 0xD08) { o->l1d_kb = 32; o->l1i_kb = 48; o->l2_kb = 1024; }
    o->temp_mc = -1; o->volt_mv = -1;
#ifdef PLATFORM_rpi5
    o->mhz = clock_mhz(3, 0); o->max_mhz = clock_mhz(3, 1);
    uint32_t a, v;
    if (mbox1(0x00030006u, 0, &a, &v) == 0) o->temp_mc = (int)v;
    if (mbox1(0x00030003u, 1, &a, &v) == 0 && v) o->volt_mv = (int)(v / 1000);
    cat(o->hyper, "", sizeof o->hyper);
#else
    cat(o->hyper, "QEMU virt", sizeof o->hyper);
#endif
    return 0;
}
int hwmon_gpu(struct hw_gpu *o)
{
    memset(o, 0, sizeof *o); o->temp_mc = -1;
#ifdef PLATFORM_rpi5
    cat(o->name, "VideoCore VII (V3D 7.1)", sizeof o->name); cat(o->vendor, "Broadcom", sizeof o->vendor);
    o->core_mhz = clock_mhz(4, 0); o->v3d_mhz = clock_mhz(5, 0);
    uint32_t base, size; if (mbox1(0x00010006u, 0, &base, &size) == 0) o->mem_mb = size >> 20;
    uint32_t a, v; if (mbox1(0x00030006u, 0, &a, &v) == 0) o->temp_mc = (int)v;
#else
    cat(o->name, "ramfb / framebuffer simple", sizeof o->name); cat(o->vendor, "QEMU", sizeof o->vendor);
#endif
    return 0;
}
int hwmon_board(struct hw_board *o)
{
    memset(o, 0, sizeof *o);
    struct ramstat_info r; ramstat_get(&r); o->ram_mb = (uint32_t)(r.total >> 20);
#ifdef PLATFORM_rpi5
    uint32_t rev = 0, x, y;
    if (mbox1(0x00010002u, 0, &rev, 0) == 0) { struct rpi_board b; rpi_board_decode(rev, &b); cat(o->model, b.name ? b.name : "Raspberry Pi", sizeof o->model); if (b.mem_mb) o->ram_mb = b.mem_mb; o->revision = rev; }
    else cat(o->model, "Raspberry Pi 5", sizeof o->model);
    if (mbox1(0x00000001u, 0, &x, 0) == 0) { cat(o->firmware, "VideoCore ", sizeof o->firmware); catn(o->firmware, x, sizeof o->firmware); }
    if (mbox1(0x00010004u, 0, &x, &y) == 0) { const char *hx = "0123456789abcdef"; char t[17]; for (int i = 0; i < 8; i++) { t[i] = hx[(y >> (28 - 4 * i)) & 15]; t[8 + i] = hx[(x >> (28 - 4 * i)) & 15]; } t[16] = 0; cat(o->serial, t, sizeof o->serial); }
    if (mbox1(0x00010006u, 0, &x, &y) == 0) o->gpu_mem_mb = y >> 20;
#else
    cat(o->model, "QEMU virt (aarch64)", sizeof o->model); cat(o->firmware, "-", sizeof o->firmware);
#endif
    return 0;
}
int hwmon_devices(struct hw_dev *o, int max)
{
    int n = 0;
#ifdef PLATFORM_rpi5
    struct { const char *bus, *name, *extra; } D[] = {
        { "SoC", "Broadcom BCM2712 (4 x Cortex-A76)", "16 nm, 2,4 GHz" }, { "SoC", "VideoCore VII", "GPU, HDMI x2" },
        { "PCIe", "RP1 (E/S del sistema)", "PCIe 2.0 x4" }, { "RP1", "Ethernet Gigabit (Cadence GEM)", "eth0" }, { "RP1", "USB 3 / USB 2 (xHCI x2)", "4 puertos" },
        { "RP1", "GPIO, UART, SPI, I2C", "40 pines" }, { "SDIO", "Infineon CYW43455", "Wi-Fi 5 + Bluetooth 5" }, { "SD", "Lector microSD (SDR104)", "SDHCI" } };
    for (unsigned i = 0; i < sizeof D / sizeof D[0] && n < max; i++) { memset(&o[n], 0, sizeof o[n]); cat(o[n].bus, D[i].bus, sizeof o[n].bus); cat(o[n].name, D[i].name, sizeof o[n].name); cat(o[n].extra, D[i].extra, sizeof o[n].extra); n++; }
#else
    if (n < max) { memset(&o[n], 0, sizeof o[n]); cat(o[n].bus, "virtio", sizeof o[n].bus); cat(o[n].name, "Dispositivos virtio (QEMU virt)", sizeof o[n].name); n++; }
#endif
    return n;
}
