/* PARCHE 061: controlador SDHCI (SD Host Controller 3.00) por sondeo, solo SD de 3,3 V con bus de 1 bit a 25 MHz.
 * Los registros de 8 y 16 bits se acceden siempre como palabras de 32 bits alineadas (lo que exigen algunos SDHCI, como el de Broadcom). */
#include <stdint.h>
#include "sdhci.h"
#include "sd_proto.h"
#include "platform.h"
#include "console.h"
#include "idt.h"

enum { E_NOHC = -1, E_RESET = -2, E_CLOCK = -3, E_NOCARD = -4, E_CMD = -5, E_TIMEOUT = -6, E_UNSUP = -7, E_DATA = -8, E_ARG = -9 };

const char *sd_err_str(int e)
{
    switch (e) {
    case 0: return "ok";
    case E_NOHC: return "esta plataforma no tiene SDHCI";
    case E_RESET: return "el controlador no sale del reinicio";
    case E_CLOCK: return "el reloj no se estabiliza";
    case E_NOCARD: return "no hay tarjeta SD";
    case E_CMD: return "la tarjeta rechazo una orden";
    case E_TIMEOUT: return "tiempo agotado";
    case E_UNSUP: return "tarjeta no soportada (solo SD de 3,3 V)";
    case E_DATA: return "error en la transferencia de datos";
    default: return "argumento no valido";
    }
}

#if defined(SOMA_SD_BASE) && SOMA_SD_BASE
#define REG(o) ((volatile uint32_t *)(uintptr_t)(SOMA_SD_BASE + (o)))
static inline uint32_t rd(uint32_t o) { return *REG(o); }
static inline void wr(uint32_t o, uint32_t v) { *REG(o) = v; }

/* acceso a campos de 8/16 bits dentro de palabras de 32 */
static void wr8(uint32_t o, uint8_t v) { uint32_t sh = (o & 3) * 8, w = rd(o & ~3u); w = (w & ~(0xFFu << sh)) | ((uint32_t)v << sh); wr(o & ~3u, w); }
static void wr16(uint32_t o, uint16_t v) { uint32_t sh = (o & 2) * 8, w = rd(o & ~3u); w = (w & ~(0xFFFFu << sh)) | ((uint32_t)v << sh); wr(o & ~3u, w); }
static uint16_t rd16(uint32_t o) { return (uint16_t)(rd(o & ~3u) >> ((o & 2) * 8)); }

#define SDMA_ADDR 0x00
#define BLK_SIZE  0x04
#define ARG       0x08
#define XFER_CMD  0x0C
#define RESP      0x10
#define BUF       0x20
#define PRESENT   0x24
#define HOSTCTL   0x28
#define PWRCTL    0x29
#define CLKCTL    0x2C
#define TIMEOUTC  0x2E
#define RESETREG  0x2F
#define INTSTAT   0x30
#define INTEN     0x34
#define SIGEN     0x38
#define CAPS      0x40

static struct sd_info g_sd; static int g_ready, g_tried;

static void wait_ms(uint32_t ms) { timer_sleep_ms(ms); }

static int wait_clear(uint32_t off, uint32_t mask, uint32_t ms)
{
    for (uint32_t i = 0; i < ms * 100; i++) { if (!(rd(off) & mask)) return 0; if ((i % 100) == 99) wait_ms(1); }
    return -1;
}

static int host_reset(void)
{
    wr8(RESETREG, 0x01);                                       /* reinicio total */
    for (int i = 0; i < 100; i++) { if (!(rd(CLKCTL) >> 24 & 1)) return 0; wait_ms(1); }
    return E_RESET;
}

static int set_clock(uint32_t hz)
{
    uint32_t caps = rd(CAPS), base = ((caps >> 8) & 0xFF) * 1000000u;
    if (base == 0) base = 200000000u;
    wr16(CLKCTL, 0);
    uint32_t n = sd_clk_div(base, hz);
    wr16(CLKCTL, (uint16_t)sd_clock_reg(n));
    for (int i = 0; i < 100; i++) { if (rd16(CLKCTL) & 2) { wr16(CLKCTL, (uint16_t)(sd_clock_reg(n) | 4)); wait_ms(2); return 0; } wait_ms(1); }
    return E_CLOCK;
}

/* Envia una orden; devuelve 0 y la respuesta en r[4]. */
static int cmd(uint8_t idx, uint32_t arg, int resp, uint32_t *r, uint32_t mode, uint32_t blk, int data)
{
    if (wait_clear(PRESENT, data ? 3u : 1u, 100)) return E_TIMEOUT;
    wr(INTSTAT, 0xFFFFFFFFu);
    if (data) wr(BLK_SIZE, blk);
    wr(ARG, arg);
    wr(XFER_CMD, (sd_cmd_word(idx, resp, data) << 16) | (mode & 0xFFFFu));
    uint32_t st = 0;
    for (uint32_t i = 0; i < 200000; i++) { st = rd(INTSTAT); if (st & ((1u << 0) | (1u << 15))) break; if ((i % 1000) == 999) wait_ms(1); }
    if (!(st & 1u) || (st & (1u << 15))) { wr(INTSTAT, 0xFFFF0000u | (st & 0xFFFFu)); wr8(RESETREG, 0x02 | 0x04); wait_ms(1); return (st & (1u << 16)) ? E_TIMEOUT : E_CMD; }
    wr(INTSTAT, 1u);
    if (r) for (int i = 0; i < 4; i++) r[i] = rd(RESP + 4u * (uint32_t)i);
    if (resp == SD_R1B) { for (uint32_t i = 0; i < 500; i++) { if (rd(PRESENT) & (1u << 20)) break; wait_ms(1); } }
    return 0;
}

static int acmd(uint8_t idx, uint32_t arg, int resp, uint32_t *r, uint16_t rca)
{
    int e = cmd(55, (uint32_t)rca << 16, SD_R1, 0, 0, 0, 0);
    return e ? e : cmd(idx, arg, resp, r, 0, 0, 0);
}

int sd_probe(struct sd_info *o)
{
    if (g_tried) { if (o) *o = g_sd; return g_ready ? 0 : g_sd.err; }
    g_tried = 1;
    for (unsigned i = 0; i < sizeof g_sd; i++) ((uint8_t *)&g_sd)[i] = 0;
    int e = host_reset();
    if (e) { g_sd.err = e; goto out; }
    wr8(PWRCTL, 0x0F);                                          /* 3,3 V encendido */
    wr8(TIMEOUTC, 0x0E);
    wr(INTEN, 0xFFFFFFFFu); wr(SIGEN, 0);
    if ((e = set_clock(400000))) { g_sd.err = e; goto out; }
    wait_ms(10);
    if (!(rd(PRESENT) & (1u << 16))) { g_sd.err = E_NOCARD; goto out; }
    uint32_t r[4];
    cmd(0, 0, SD_R_NONE, 0, 0, 0, 0);
    wait_ms(2);
    int v2 = cmd(8, 0x1AA, SD_R7, r, 0, 0, 0) == 0 && (r[0] & 0xFFF) == 0x1AA;
    uint32_t ocr = 0; int ok = 0;
    for (int i = 0; i < 200; i++) {
        if (acmd(41, 0x00FF8000u | (v2 ? 0x40000000u : 0), SD_R3, r, 0)) { g_sd.err = E_UNSUP; goto out; }
        if (r[0] & 0x80000000u) { ocr = r[0]; ok = 1; break; }
        wait_ms(5);
    }
    if (!ok) { g_sd.err = E_TIMEOUT; goto out; }
    g_sd.sdhc = (ocr >> 30) & 1;
    if ((e = cmd(2, 0, SD_R2, r, 0, 0, 0))) { g_sd.err = e; goto out; }
    if ((e = cmd(3, 0, SD_R6, r, 0, 0, 0))) { g_sd.err = e; goto out; }
    g_sd.rca = (uint16_t)(r[0] >> 16);
    if ((e = cmd(9, (uint32_t)g_sd.rca << 16, SD_R2, r, 0, 0, 0))) { g_sd.err = e; goto out; }
    if (sd_csd_sectors(r, &g_sd.sectors)) { g_sd.err = E_UNSUP; goto out; }
    if ((e = cmd(7, (uint32_t)g_sd.rca << 16, SD_R1B, 0, 0, 0, 0))) { g_sd.err = e; goto out; }
    set_clock(25000000);
    g_ready = 1;
    {   /* sector 0 para identificar discos de prueba */
        static uint8_t s0[512];
        if (sd_read(0, 1, s0) == 0) for (int i = 0; i < 16; i++) g_sd.sector0[i] = s0[i];
    }
out:
    if (o) *o = g_sd;
    return g_ready ? 0 : g_sd.err;
}

static int xfer(uint64_t lba, uint32_t n, uint8_t *buf, int wrt)
{
    if (!g_ready) return E_NOCARD;
    if (n == 0 || n > 128 || lba >= g_sd.sectors || n > g_sd.sectors - lba) return E_ARG;
    for (uint32_t b = 0; b < n; b++) {                          /* un bloque por orden: mas sencillo y robusto en PIO */
        uint64_t a = lba + b;
        uint32_t arg = g_sd.sdhc ? (uint32_t)a : (uint32_t)(a << 9);
        uint32_t mode = wrt ? 0u : (1u << 4);                   /* sentido de lectura */
        int e = cmd(wrt ? 24 : 17, arg, SD_R1, 0, mode, (7u << 12) | 512u, 1);
        if (e) return e;
        uint32_t flag = wrt ? (1u << 4) : (1u << 5), st = 0;
        for (uint32_t i = 0; i < 300000; i++) { st = rd(INTSTAT); if (st & (flag | (1u << 15))) break; if ((i % 1000) == 999) wait_ms(1); }
        if (!(st & flag)) { wr(INTSTAT, 0xFFFFFFFFu); wr8(RESETREG, 0x06); return E_DATA; }
        wr(INTSTAT, flag);
        uint8_t *p = buf + (uint64_t)b * 512;
        for (int i = 0; i < 128; i++) {
            if (wrt) { uint32_t v = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 | (uint32_t)p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24; wr(BUF, v); }
            else { uint32_t v = rd(BUF); p[4 * i] = (uint8_t)v; p[4 * i + 1] = (uint8_t)(v >> 8); p[4 * i + 2] = (uint8_t)(v >> 16); p[4 * i + 3] = (uint8_t)(v >> 24); }
        }
        for (uint32_t i = 0; i < 300000; i++) { st = rd(INTSTAT); if (st & ((1u << 1) | (1u << 15))) break; if ((i % 1000) == 999) wait_ms(1); }
        if (!(st & 2u)) { wr(INTSTAT, 0xFFFFFFFFu); wr8(RESETREG, 0x06); return E_DATA; }
        wr(INTSTAT, 2u);
    }
    return 0;
}
int sd_read(uint64_t lba, uint32_t n, void *buf) { return xfer(lba, n, (uint8_t *)buf, 0); }
int sd_write(uint64_t lba, uint32_t n, const void *buf) { return xfer(lba, n, (uint8_t *)(uintptr_t)buf, 1); }
#else
int sd_probe(struct sd_info *o) { (void)o; return E_NOHC; }
int sd_read(uint64_t lba, uint32_t n, void *buf) { (void)lba; (void)n; (void)buf; return E_NOHC; }
int sd_write(uint64_t lba, uint32_t n, const void *buf) { (void)lba; (void)n; (void)buf; return E_NOHC; }
static struct sd_info g_sd; static int g_ready;
#endif

int sd_ready(void) { return g_ready; }
const struct sd_info *sd_get(void) { return &g_sd; }

void sd_autostart(void)
{
#if defined(SOMA_SD_BASE) && SOMA_SD_BASE
    struct sd_info si;
    int r = sd_probe(&si);
    if (r == 0) { kprint("SD: tarjeta "); kprint(si.sdhc ? "SDHC/SDXC" : "SD"); kprint(" de "); kprint_dec((uint32_t)(si.sectors / 2048)); kprint(" MiB (usa 'blk')\n"); }
    else { kprint("SD: "); kprint(sd_err_str(r)); kprint("\n"); }
#endif
}
