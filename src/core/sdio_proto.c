/* PARCHE 079: partes puras del Wi-Fi (SDIO + chip Broadcom/Cypress). Ver sdio_proto.h. */
#include "sdio_proto.h"

uint32_t sdio_cmd52_arg(int write, unsigned fn, uint32_t addr, uint8_t data, int raw)
{
    return (write ? 1u << 31 : 0) | ((fn & 7u) << 28) | (raw ? 1u << 27 : 0) | ((addr & 0x1FFFFu) << 9) | data;
}

uint32_t sdio_cmd53_arg(int write, unsigned fn, int block, int incr, uint32_t addr, uint32_t count)
{
    if (count == 0 || count > 512 || addr > 0x1FFFFu || fn > 7) return 0;
    return (write ? 1u << 31 : 0) | ((fn & 7u) << 28) | (block ? 1u << 27 : 0) | (incr ? 1u << 26 : 0) | (addr << 9) | (count & 0x1FFu);
}

int sdio_r5_ok(uint32_t r5) { return ((r5 >> 8) & 0xCBu) == 0; }

int sdio_cis_manfid(const uint8_t *cis, uint32_t len, uint16_t *manf, uint16_t *card)
{
    uint32_t i = 0;
    while (i < len) {
        uint8_t code = cis[i];
        if (code == 0xFF) return -1;                         /* fin de cadena */
        if (code == 0x00) { i++; continue; }                 /* tupla nula: sin enlace */
        if (i + 1 >= len) return -1;
        uint8_t link = cis[i + 1];
        if (code == 0x20) {
            if (link < 4 || i + 2 + 4 > len) return -1;
            if (manf) *manf = (uint16_t)(cis[i + 2] | cis[i + 3] << 8);
            if (card) *card = (uint16_t)(cis[i + 4] | cis[i + 5] << 8);
            return 0;
        }
        if (link == 0xFF) return -1;
        i += 2u + link;
    }
    return -1;
}

void sb_window(uint32_t addr, uint8_t out[3])
{
    out[0] = (uint8_t)((addr >> 8) & 0x80u);
    out[1] = (uint8_t)(addr >> 16);
    out[2] = (uint8_t)(addr >> 24);
}

void brcm_chipid_decode(uint32_t v, struct brcm_chipid *o)
{
    o->id = (uint16_t)(v & 0xFFFFu);
    o->rev = (uint8_t)((v >> 16) & 0xFu);
    o->pkg = (uint8_t)((v >> 20) & 0xFu);
    o->ncores = (uint8_t)((v >> 24) & 0xFu);
    o->socitype = (uint8_t)((v >> 28) & 0xFu);
}

/* Descriptores de la EROM (formato "DMP" de los chips AI de Broadcom) */
#define ER_VALID   1u
#define ER_TAG     0xEu
#define ER_CI      0x0u
#define ER_MP      0x2u
#define ER_ADDR    0x4u
#define ER_END     0xFu
#define AD_AG32    0x8u
#define AD_SZ      0x30u
#define AD_SZ_SD   0x30u
#define AD_TYPE    0xC0u
#define AD_SLAVE   0x00u
#define AD_SWRAP   0x80u
#define AD_MWRAP   0xC0u
#define SZ_SG32    0x8u

int brcm_erom_parse(const uint32_t *w, unsigned nwords, struct brcm_core *out, int max)
{
    unsigned i = 0; int n = 0;
    while (i < nwords) {
        uint32_t v = w[i];
        if (v == ER_END) return n;
        if (!(v & ER_VALID) || (v & ER_TAG) != ER_CI) { i++; continue; }        /* hueco o descriptor suelto */
        if (i + 1 >= nwords) return -1;
        uint32_t cia = v, cib = w[i + 1];
        if (!(cib & ER_VALID) || (cib & ER_TAG) != ER_CI) return -1;
        i += 2;
        uint16_t id = (uint16_t)((cia >> 8) & 0xFFFu);
        uint8_t rev = (uint8_t)(cib >> 24);
        unsigned nmw = (cib >> 14) & 0x1Fu, nsw = (cib >> 19) & 0x1Fu;
        uint32_t base = 0, swrap = 0, mwrap = 0;
        while (i < nwords) {
            uint32_t d = w[i];
            if (d == ER_END) break;
            if ((d & ER_VALID) && (d & ER_TAG) == ER_CI) break;                /* siguiente nucleo */
            i++;
            if (!(d & ER_VALID)) continue;
            if ((d & ER_TAG) == ER_MP) continue;
            if ((d & ER_TAG) != ER_ADDR) continue;
            uint32_t a = d & 0xFFFFF000u;
            if (d & AD_AG32) { if (i >= nwords) return -1; i++; }                /* parte alta (los chips SDIO estan en 32 bits) */
            if ((d & AD_SZ) == AD_SZ_SD) {                                       /* descriptor de tamano detras */
                if (i >= nwords) return -1;
                uint32_t sz = w[i++];
                if (sz & SZ_SG32) { if (i >= nwords) return -1; i++; }
            }
            uint32_t type = d & AD_TYPE, port = (d >> 8) & 0xFu;
            if (type == AD_SLAVE && port == 0 && !base) base = a;
            else if (type == AD_SWRAP && !swrap) swrap = a;
            else if (type == AD_MWRAP && !mwrap) mwrap = a;
        }
        if (nmw + nsw == 0 || !base) continue;                                   /* sin envoltorio: no es un nucleo gestionable */
        if (n < max) { out[n].id = id; out[n].rev = rev; out[n].base = base; out[n].wrap = nmw ? mwrap : swrap; }
        n++;
    }
    return -1;                                                                    /* sin marca de fin */
}

const char *brcm_core_name(uint16_t id)
{
    switch (id) {
    case CORE_CHIPCOMMON: return "chipcommon";
    case CORE_SOCRAM: return "socram";
    case CORE_D11: return "802.11 (D11)";
    case CORE_PMU: return "PMU";
    case CORE_SDIO_DEV: return "SDIO";
    case CORE_ARM_CR4: return "ARM CR4";
    case 0x135: return "gci";
    case 0x240: return "default";
    case 0x367: return "oob";
    default: return "?";
    }
}

uint32_t brcm_cr4_bank_bytes(uint32_t bankinfo)
{
    uint32_t mult = (bankinfo & 0x200u) ? 1024u : 8192u;
    return ((bankinfo & 0x7Fu) + 1u) * mult;
}

uint32_t brcm_rambase(uint16_t chip)
{
    switch (chip) {
    case 0x4345: case 0xA9BF: case 0xA9C6: return 0x198000u;   /* 43455 / 43456 (Pi 3B+, 4, 5, 400, Zero 2 W con 43436 no) */
    case 0x4339: case 0x4335: return 0x180000u;
    case 0x4354: case 0x4356: return 0x180000u;
    default: return 0;
    }
}

int brcm_nvram_pack(const char *txt, uint32_t len, uint8_t *out, uint32_t max)
{
    uint32_t o = 0, i = 0;
    while (i < len) {
        uint32_t s = i;
        while (i < len && txt[i] != '\n') i++;
        uint32_t e = i;
        if (i < len) i++;
        while (s < e && (txt[s] == ' ' || txt[s] == '\t')) s++;
        while (e > s && (txt[e - 1] == '\r' || txt[e - 1] == ' ' || txt[e - 1] == '\t')) e--;
        if (s == e || txt[s] == '#') continue;
        int has_eq = 0;
        for (uint32_t k = s; k < e; k++) if (txt[k] == '=') { has_eq = k > s; break; }
        if (!has_eq) continue;
        if (o + (e - s) + 1 > max) return -1;
        for (uint32_t k = s; k < e; k++) out[o++] = (uint8_t)txt[k];
        out[o++] = 0;
    }
    if (o + 1 > max) return -1;
    out[o++] = 0;                                    /* fin de la lista */
    while (o & 3u) { if (o >= max) return -1; out[o++] = 0; }
    if (o + 4 > max) return -1;
    uint32_t words = o / 4, tok = (~words << 16) | (words & 0xFFFFu);
    out[o++] = (uint8_t)tok; out[o++] = (uint8_t)(tok >> 8); out[o++] = (uint8_t)(tok >> 16); out[o++] = (uint8_t)(tok >> 24);
    return (int)o;
}

int brcm_is_nvram_token(uint32_t v) { return (v & 0xFFFFu) == (~(v >> 16) & 0xFFFFu); }

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int brcm_shared_parse(const uint8_t *b, uint32_t len, struct brcm_shared *o)
{
    if (len < 28) return -1;
    o->flags = le32(b); o->trap_addr = le32(b + 4); o->assert_exp = le32(b + 8); o->assert_file = le32(b + 12);
    o->assert_line = le32(b + 16); o->console_addr = le32(b + 20); o->msgtrace = le32(b + 24);
    uint32_t v = o->flags & SHARED_VERSION_MASK;
    return (v >= 1 && v <= 4) ? 0 : -2;
}

void rpi_board_decode(uint32_t rev, struct rpi_board *o)
{
    static const uint32_t mem[8] = { 256, 512, 1024, 2048, 4096, 8192, 16384, 0 };
    o->new_style = (rev >> 23) & 1u;
    o->type = (rev >> 4) & 0xFFu; o->rev = rev & 0xFu; o->maker = (rev >> 16) & 0xFu;
    o->mem_mb = o->new_style ? mem[(rev >> 20) & 7u] : 0;
    o->name = "desconocida"; o->has_wifi = 0;
    if (!o->new_style) { o->name = "modelo antiguo"; return; }
    switch (o->type) {
    case 0x08: o->name = "Raspberry Pi 3 B"; o->has_wifi = 1; break;
    case 0x0C: o->name = "Raspberry Pi Zero W"; o->has_wifi = 1; break;
    case 0x0D: o->name = "Raspberry Pi 3 B+"; o->has_wifi = 1; break;
    case 0x0E: o->name = "Raspberry Pi 3 A+"; o->has_wifi = 1; break;
    case 0x11: o->name = "Raspberry Pi 4 B"; o->has_wifi = 1; break;
    case 0x12: o->name = "Raspberry Pi Zero 2 W"; o->has_wifi = 1; break;
    case 0x13: o->name = "Raspberry Pi 400"; o->has_wifi = 1; break;
    case 0x14: o->name = "Compute Module 4"; o->has_wifi = 1; break;
    case 0x17: o->name = "Raspberry Pi 5"; o->has_wifi = 1; break;
    case 0x18: o->name = "Compute Module 5"; o->has_wifi = 1; break;
    case 0x19: o->name = "Raspberry Pi 500"; o->has_wifi = 1; break;
    case 0x1A: o->name = "Compute Module 5 Lite"; o->has_wifi = 1; break;
    default: break;
    }
}
