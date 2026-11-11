/* SOMA_PARCHE_020_AUDIO: controlador Intel HD Audio. Ver hda.h.
 * Comandos al codec por la interfaz de comandos inmediatos (ICOI/IRII/ICIS); salida por el flujo 0 de salida con
 * una lista de descriptores (BDL) sobre un anillo contiguo. */
#include <stdint.h>
#include <stddef.h>
#include "hda.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "console.h"
#include "idt.h"
#include "arch.h"

#define REG_GCAP     0x00
#define REG_GCTL     0x08
#define REG_STATESTS 0x0E
#define REG_INTCTL   0x20
#define REG_ICOI     0x60
#define REG_IRII     0x64
#define REG_ICIS     0x68
#define SD_CTL   0x00
#define SD_STS   0x03
#define SD_LPIB  0x04
#define SD_CBL   0x08
#define SD_LVI   0x0C
#define SD_FMT   0x12
#define SD_BDPL  0x18
#define SD_BDPU  0x1C

#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)

#define RING_BYTES   (128 * 1024)
#define BDL_ENTRY    (16 * 1024)                 /* 8 entradas de 16 KiB */
#define STREAM_TAG   1

static volatile uint8_t *mmio;
static int ready;
static uint32_t pci_id, mmio_phys, gcap;
static uint8_t cad, afg, dac_nid, pin_nid;
static uint8_t path[8]; static int path_len;
static uint8_t sel_idx[8];                       /* para cada nodo del camino, que entrada del selector/mezclador usar */
static uint32_t codec_vendor;
static uint32_t sd_base;
static uint8_t *ring; static uintptr_t ring_phys;
static uint8_t *bdl; static uintptr_t bdl_phys;
static uint32_t cur_rate;
static int volume_pct = 100;
static uint32_t dac_amp_steps, pin_amp_steps;     /* numsteps de los amplificadores de salida del DAC y del pin */
static int dac_has_amp, pin_has_amp;

static inline uint8_t  rd8(uint32_t r)  { return *(volatile uint8_t *)(mmio + r); }
static inline uint16_t rd16(uint32_t r) { return *(volatile uint16_t *)(mmio + r); }
static inline uint32_t rd32(uint32_t r) { return *(volatile uint32_t *)(mmio + r); }
static inline void wr8(uint32_t r, uint8_t v)   { *(volatile uint8_t *)(mmio + r) = v; }
static inline void wr16(uint32_t r, uint16_t v) { *(volatile uint16_t *)(mmio + r) = v; }
static inline void wr32(uint32_t r, uint32_t v) { *(volatile uint32_t *)(mmio + r) = v; }

static void wait_ms(uint32_t ms) { uint64_t t = timer_get_uptime_ms() + ms + 1; while (timer_get_uptime_ms() < t) arch_pause(); }

/* ---- comandos al codec ---- */
static int codec_cmd(uint32_t verb_payload, uint32_t *resp)
{
    uint64_t t = timer_get_uptime_ms() + 20;
    while (rd16(REG_ICIS) & 1) { if (timer_get_uptime_ms() > t) return -1; arch_pause(); }
    wr32(REG_ICOI, ((uint32_t)cad << 28) | verb_payload);
    wr16(REG_ICIS, 0x2);                                  /* limpia IRV */
    wr16(REG_ICIS, 0x1);                                  /* envia */
    t = timer_get_uptime_ms() + 50;
    while ((rd16(REG_ICIS) & 0x3) != 0x2) { if (timer_get_uptime_ms() > t) return -2; arch_pause(); }
    if (resp) *resp = rd32(REG_IRII);
    return 0;
}
#define V12(nid, verb, payload) (((uint32_t)(nid) << 20) | ((uint32_t)(verb) << 8) | ((payload) & 0xFF))
#define V4(nid, verb, payload)  (((uint32_t)(nid) << 20) | ((uint32_t)(verb) << 16) | ((payload) & 0xFFFF))

static uint32_t get_param(uint8_t nid, uint8_t param) { uint32_t r = 0; codec_cmd(V12(nid, 0xF00, param), &r); return r; }
static uint32_t get_verb(uint8_t nid, uint32_t verb, uint8_t payload) { uint32_t r = 0; codec_cmd(V12(nid, verb, payload), &r); return r; }
static void set_verb(uint8_t nid, uint32_t verb, uint8_t payload) { codec_cmd(V12(nid, verb, payload), 0); }

/* lista de conexiones de un widget (entradas de 8 o 16 bits); devuelve cuantas */
static int conn_list(uint8_t nid, uint8_t *out, int max)
{
    uint32_t len = get_param(nid, 0x0E);
    int n = (int)(len & 0x7F), lng = (len >> 7) & 1, got = 0;
    for (int i = 0; i < n && got < max; ) {
        uint32_t r = get_verb(nid, 0xF02, (uint8_t)i);
        if (lng) { for (int k = 0; k < 2 && i < n && got < max; ++k, ++i) out[got++] = (uint8_t)((r >> (16 * k)) & 0xFF); }
        else     { for (int k = 0; k < 4 && i < n && got < max; ++k, ++i) out[got++] = (uint8_t)((r >> (8 * k)) & 0xFF); }
    }
    return got;
}

static int widget_type(uint8_t nid) { return (int)((get_param(nid, 0x09) >> 20) & 0xF); }

/* busca un DAC alcanzable desde `nid` (a traves de selectores/mezcladores, hasta profundidad 3) y rellena path[] */
static int find_dac(uint8_t nid, int depth, uint8_t *stack, uint8_t *sel, int *len)
{
    uint8_t cl[16]; int n = conn_list(nid, cl, 16);
    for (int i = 0; i < n; ++i) {
        int t = widget_type(cl[i]);
        if (t == 0) { stack[*len] = cl[i]; sel[*len] = 0; (*len)++; return 1; }
        if ((t == 2 || t == 3) && depth < 3) {
            int save = *len;
            stack[*len] = cl[i]; sel[*len] = 0; (*len)++;
            if (find_dac(cl[i], depth + 1, stack, sel, len)) return 1;
            *len = save;
        }
    }
    return 0;
}

static void set_amp_out(uint8_t nid, int pct, uint32_t steps)
{
    uint32_t gain = steps ? (uint32_t)pct * steps / 100 : 0;
    uint32_t mute = pct == 0 ? 0x80 : 0;
    codec_cmd(V4(nid, 0x3, 0xB000 | mute | (gain & 0x7F)), 0);        /* salida, izquierdo y derecho */
}
static void set_amp_in(uint8_t nid, int idx, int pct, uint32_t steps)
{
    uint32_t gain = steps ? (uint32_t)pct * steps / 100 : 0;
    uint32_t mute = pct == 0 ? 0x80 : 0;
    codec_cmd(V4(nid, 0x3, 0x7000 | ((uint32_t)idx << 8) | mute | (gain & 0x7F)), 0);
}

static uint32_t out_amp_steps(uint8_t nid)
{
    uint32_t caps = get_param(nid, 0x09);
    if (!(caps & (1u << 2))) return 0;                                  /* sin amplificador de salida */
    uint32_t a = (caps & (1u << 3)) ? get_param(nid, 0x12) : get_param(afg, 0x12);
    return (a >> 8) & 0x7F;
}

int hda_set_volume(int pct)
{
    if (!ready) return -1;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    volume_pct = pct;
    for (int i = 0; i < path_len; ++i) {
        uint32_t caps = get_param(path[i], 0x09);
        if (caps & (1u << 2)) set_amp_out(path[i], pct, out_amp_steps(path[i]));
        if (caps & (1u << 1)) set_amp_in(path[i], sel_idx[i], pct, 0);   /* entrada: se deja a 0 dB (gain 0 con mute off) */
    }
    return 0;
}

/* ---- formato de flujo ---- */
int hda_format_for_rate(uint32_t rate, uint16_t *fmt)
{
    static const struct { uint32_t r; uint16_t f; } T[] = {
        { 48000, 0x0000 }, { 44100, 0x4000 }, { 32000, 0x0A00 /* x2 / 3 */ }, { 24000, 0x0100 }, { 22050, 0x4100 },
        { 16000, 0x0200 }, { 11025, 0x4300 }, { 8000, 0x0500 }, { 96000, 0x0800 }, { 88200, 0x4800 },
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); ++i)
        if (T[i].r == rate) { *fmt = (uint16_t)(T[i].f | 0x0011); return 0; }    /* 16 bits, 2 canales */
    return -1;
}

/* ---- flujo de salida ---- */
static int stream_reset(void)
{
    wr8(sd_base + SD_CTL, 0);
    uint64_t t = timer_get_uptime_ms() + 20;
    while (rd8(sd_base + SD_CTL) & 2) { if (timer_get_uptime_ms() > t) break; arch_pause(); }
    wr8(sd_base + SD_CTL, 1);                                          /* SRST */
    t = timer_get_uptime_ms() + 20;
    while (!(rd8(sd_base + SD_CTL) & 1)) { if (timer_get_uptime_ms() > t) return -1; arch_pause(); }
    wr8(sd_base + SD_CTL, 0);
    t = timer_get_uptime_ms() + 20;
    while (rd8(sd_base + SD_CTL) & 1) { if (timer_get_uptime_ms() > t) return -2; arch_pause(); }
    return 0;
}

static int hw_start(void *ctx, uint32_t rate)
{
    (void)ctx;
    uint16_t fmt;
    if (!ready || hda_format_for_rate(rate, &fmt) != 0) return -1;
    if (stream_reset() != 0) return -2;
    for (uint32_t i = 0; i < RING_BYTES; ++i) ring[i] = 0;
    const uint32_t nent = RING_BYTES / BDL_ENTRY;
    for (uint32_t e = 0; e < nent; ++e) {
        uint32_t *d = (uint32_t *)(bdl + e * 16);
        uintptr_t a = ring_phys + (uintptr_t)e * BDL_ENTRY;
        d[0] = (uint32_t)a; d[1] = (uint32_t)((uint64_t)a >> 32); d[2] = BDL_ENTRY; d[3] = 0;     /* sin IOC */
    }
    wr32(sd_base + SD_BDPL, (uint32_t)bdl_phys); wr32(sd_base + SD_BDPU, 0);
    wr32(sd_base + SD_CBL, RING_BYTES);
    wr16(sd_base + SD_LVI, (uint16_t)(nent - 1));
    wr16(sd_base + SD_FMT, fmt);
    wr8(sd_base + SD_CTL + 2, (uint8_t)(STREAM_TAG << 4));             /* numero de flujo (bits 23:20) */
    /* codec: formato del DAC y canal/flujo */
    codec_cmd(V4(dac_nid, 0x2, fmt), 0);
    codec_cmd(V12(dac_nid, 0x706, (STREAM_TAG << 4) | 0), 0);
    cur_rate = rate;
    wr8(sd_base + SD_CTL, rd8(sd_base + SD_CTL) | 2);                  /* RUN */
    return 0;
}

static void hw_stop(void *ctx)
{
    (void)ctx;
    if (!ready) return;
    wr8(sd_base + SD_CTL, rd8(sd_base + SD_CTL) & (uint8_t)~2);
    uint64_t t = timer_get_uptime_ms() + 20;
    while (rd8(sd_base + SD_CTL) & 2) { if (timer_get_uptime_ms() > t) break; arch_pause(); }
}

static uint32_t hw_pos(void *ctx) { (void)ctx; return rd32(sd_base + SD_LPIB) % RING_BYTES; }

static struct audio_hw hw_ops;
const struct audio_hw *hda_hw(void) { return &hw_ops; }
int hda_ready(void) { return ready; }

/* ---- inicializacion ---- */
static int find_controller(uint8_t *bus, uint8_t *slot, uint8_t *fn)
{
    for (int b = 0; b < 4; ++b)
        for (int s = 0; s < 32; ++s)
            for (int f = 0; f < 8; ++f) {
                if (pci_vendor_id((uint8_t)b, (uint8_t)s, (uint8_t)f) == 0xFFFF) { if (f == 0) break; continue; }
                if ((pci_config_read32((uint8_t)b, (uint8_t)s, (uint8_t)f, 0x08) >> 16) == 0x0403) {
                    *bus = (uint8_t)b; *slot = (uint8_t)s; *fn = (uint8_t)f; return 1;
                }
            }
    return 0;
}

int hda_init(void)
{
    kprint("\nAUDIO (Intel HDA)\n-----------------\n");
    uint8_t bus, slot, fn;
    if (!find_controller(&bus, &slot, &fn)) { kprint("hda: no hay controlador de audio (QEMU: -device intel-hda -device hda-duplex)\n"); return -1; }
    pci_id = pci_config_read32(bus, slot, fn, 0x00);
    uint32_t bar = pci_bar0(bus, slot, fn);
    if (bar & 1) { kprint("hda: BAR0 no es de memoria\n"); return -2; }
    mmio_phys = bar & ~0xFu;
    pci_config_write32(bus, slot, fn, 0x04, pci_config_read32(bus, slot, fn, 0x04) | 0x6);      /* memoria + bus master */
    for (uint32_t off = 0; off < 0x4000; off += 4096)
        if (vmm_map_page(mmio_phys + off, mmio_phys + off, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | PTE_PWT | PTE_PCD) != 0) {
            kprint("hda: no se pudo mapear el MMIO\n"); return -3;
        }
    mmio = (volatile uint8_t *)(uintptr_t)mmio_phys;
    kprint("hda: controlador "); kprint_hex16((uint16_t)(pci_id & 0xFFFF)); kprint(":"); kprint_hex16((uint16_t)(pci_id >> 16));
    kprint(" en PCI "); kprint_dec(bus); kprint(":"); kprint_dec(slot); kprint(" MMIO 0x"); kprint_hex32(mmio_phys); kprint("\n");

    /* reinicio del controlador */
    wr32(REG_GCTL, 0);
    uint64_t t = timer_get_uptime_ms() + 100;
    while (rd32(REG_GCTL) & 1) { if (timer_get_uptime_ms() > t) { kprint("hda: no sale de CRST\n"); return -4; } arch_pause(); }
    wait_ms(2);
    wr32(REG_GCTL, 1);
    t = timer_get_uptime_ms() + 100;
    while (!(rd32(REG_GCTL) & 1)) { if (timer_get_uptime_ms() > t) { kprint("hda: no entra en CRST\n"); return -5; } arch_pause(); }
    wait_ms(10);                                                      /* los codecs tardan unos instantes en anunciarse */
    wr32(REG_INTCTL, 0);
    gcap = rd16(REG_GCAP);
    uint32_t iss = (gcap >> 8) & 0xF, oss = (gcap >> 12) & 0xF;
    if (oss == 0) { kprint("hda: el controlador no tiene flujos de salida\n"); return -6; }
    sd_base = 0x80 + 0x20 * iss;                                      /* primer flujo de salida */
    uint16_t st = rd16(REG_STATESTS);
    if (!st) { kprint("hda: ningun codec (STATESTS=0)\n"); return -7; }
    for (cad = 0; cad < 15 && !(st & (1u << cad)); ++cad) { }
    codec_vendor = get_param(0, 0x00);
    kprint("hda: codec "); kprint_dec(cad); kprint(" vendor 0x"); kprint_hex32(codec_vendor);
    kprint(", flujos: "); kprint_dec(iss); kprint(" entrada / "); kprint_dec(oss); kprint(" salida\n");

    /* grupo de funcion de audio */
    uint32_t sub = get_param(0, 0x04);
    uint8_t fg0 = (uint8_t)((sub >> 16) & 0xFF); int nfg = (int)(sub & 0xFF);
    afg = 0;
    for (int i = 0; i < nfg; ++i) if ((get_param((uint8_t)(fg0 + i), 0x05) & 0xFF) == 1) { afg = (uint8_t)(fg0 + i); break; }
    if (!afg) { kprint("hda: el codec no tiene grupo de funcion de audio\n"); return -8; }
    set_verb(afg, 0x705, 0);                                          /* D0 */
    wait_ms(2);

    /* pin de salida con un DAC alcanzable */
    uint32_t wsub = get_param(afg, 0x04);
    uint8_t w0 = (uint8_t)((wsub >> 16) & 0xFF); int nw = (int)(wsub & 0xFF);
    pin_nid = 0; dac_nid = 0; path_len = 0;
    for (int i = 0; i < nw && !pin_nid; ++i) {
        uint8_t nid = (uint8_t)(w0 + i);
        if (widget_type(nid) != 4) continue;
        if (!(get_param(nid, 0x0C) & (1u << 4))) continue;            /* sin capacidad de salida */
        uint32_t cfg = get_verb(nid, 0xF1C, 0);
        if (((cfg >> 30) & 3) == 1) continue;                         /* sin conexion fisica */
        uint8_t stack[8], sel[8]; int len = 0;
        if (find_dac(nid, 0, stack, sel, &len)) {
            pin_nid = nid; dac_nid = stack[len - 1];
            path_len = 0; path[path_len] = pin_nid; sel_idx[path_len++] = 0;
            for (int k = len - 2; k >= 0; --k) { path[path_len] = stack[k]; sel_idx[path_len++] = 0; }
            path[path_len] = dac_nid; sel_idx[path_len++] = 0;
        }
    }
    if (!pin_nid) { kprint("hda: no encuentro un camino pin de salida -> DAC\n"); return -9; }

    /* energia, amplificadores desmutados al maximo y pin habilitado */
    for (int i = 0; i < path_len; ++i) set_verb(path[i], 0x705, 0);
    dac_has_amp = (int)out_amp_steps(dac_nid) > 0; dac_amp_steps = out_amp_steps(dac_nid);
    pin_has_amp = (int)out_amp_steps(pin_nid) > 0; pin_amp_steps = out_amp_steps(pin_nid);
    hda_set_volume(100);
    set_verb(pin_nid, 0x707, 0x40);                                   /* OUT_EN */
    if (get_param(pin_nid, 0x0C) & (1u << 16)) set_verb(pin_nid, 0x70C, 2);   /* EAPD */

    /* anillo contiguo de 128 KiB y lista de descriptores */
    ring_phys = pmm_alloc_contig(RING_BYTES / 4096);
    bdl_phys = pmm_alloc_frame();
    if (!ring_phys || !bdl_phys) { kprint("hda: sin memoria para el DMA\n"); return -10; }
    ring = (uint8_t *)ring_phys; bdl = (uint8_t *)bdl_phys;
    for (uint32_t i = 0; i < RING_BYTES; ++i) ring[i] = 0;
    hw_ops.ctx = 0; hw_ops.ring = ring; hw_ops.ring_bytes = RING_BYTES;
    hw_ops.start = hw_start; hw_ops.stop = hw_stop; hw_ops.pos = hw_pos;
    ready = 1;
    kprint("hda: salida lista: pin "); kprint_dec(pin_nid); kprint(" -> DAC "); kprint_dec(dac_nid);
    kprint(", anillo DMA de "); kprint_dec(RING_BYTES / 1024); kprint(" KiB\n");
    return 0;
}

void hda_info(char *out, uint32_t max)
{
    uint32_t pos = 0;
    #define S(str) do { const char *_s = (str); while (*_s && pos + 1 < max) out[pos++] = *_s++; } while (0)
    #define N(v) do { char _b[24]; int _i = 23; uint64_t _v = (uint64_t)(v); _b[_i] = 0; if (!_v) _b[--_i] = '0'; while (_v) { _b[--_i] = (char)('0' + _v % 10); _v /= 10; } S(_b + _i); } while (0)
    if (!ready) { S("audio: sin dispositivo"); }
    else {
        S("audio: Intel HDA, codec "); N(cad); S(", pin "); N(pin_nid); S(" -> DAC "); N(dac_nid);
        S(", frecuencia actual "); N(cur_rate); S(" Hz, volumen "); N(volume_pct); S(" %");
    }
    out[pos < max ? pos : max - 1] = 0;
    #undef S
    #undef N
}
