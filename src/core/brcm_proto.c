/* PARCHE 081: protocolo de control del firmware Broadcom FullMAC (SDPCM + BCDC). Ver brcm_proto.h. */
#include "brcm_proto.h"

static void w16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t b16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t b32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void zero(uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = 0; }

void sdpcm_build(uint8_t *o, uint16_t len, uint8_t seq, uint8_t chan)
{
    w16(o, len); w16(o + 2, (uint16_t)~len);
    o[4] = seq; o[5] = chan & 0x0F; o[6] = 0; o[7] = SDPCM_HDR; o[8] = 0; o[9] = 0; o[10] = 0; o[11] = 0;
}
int sdpcm_parse(const uint8_t *b, size_t avail, struct sdpcm *o)
{
    if (avail < 4) return -1;
    o->len = r16(b);
    if (o->len == 0 && r16(b + 2) == 0) return 0;                 /* nada pendiente */
    if ((uint16_t)(o->len ^ r16(b + 2)) != 0xFFFFu) return -1;
    if (o->len < SDPCM_HDR || avail < SDPCM_HDR) return -1;
    o->seq = b[4]; o->chan = b[5] & 0x0F; o->next = b[6]; o->doff = b[7]; o->fc = b[8]; o->credit = b[9];
    if (o->doff < SDPCM_HDR || o->doff > o->len) return -2;
    return 0;
}
int sdpcm_can_send(uint8_t credit, uint8_t seq) { uint8_t d = (uint8_t)(credit - seq); return d != 0 && !(d & 0x80); }

size_t bcdc_build(uint8_t *o, uint32_t cmd, int set, uint16_t id, const void *payload, uint32_t len)
{
    w32(o, cmd); w32(o + 4, len); w32(o + 8, ((uint32_t)id << 16) | (set ? BCDC_F_SET : 0)); w32(o + 12, 0);
    const uint8_t *p = (const uint8_t *)payload;
    for (uint32_t i = 0; i < len; i++) o[BCDC_HDR + i] = p ? p[i] : 0;
    return BCDC_HDR + len;
}
int bcdc_parse(const uint8_t *b, size_t len, struct bcdc *o)
{
    if (len < BCDC_HDR) return -1;
    o->cmd = r32(b); o->len = r32(b + 4); o->flags = r32(b + 8); o->status = r32(b + 12); o->id = (uint16_t)(o->flags >> 16);
    return 0;
}
size_t bcdc_data_build(uint8_t *o) { o[0] = 0x20; o[1] = 0; o[2] = 0; o[3] = 0; return BCDC_DATA_HDR; }
int bcdc_data_skip(const uint8_t *b, size_t len)
{
    if (len < BCDC_DATA_HDR || (b[0] >> 4) != 2) return -1;
    size_t s = BCDC_DATA_HDR + (size_t)b[3] * 4u;
    return s <= len ? (int)s : -1;
}

size_t brcm_iovar(uint8_t *o, size_t cap, const char *name, const void *val, size_t vlen)
{
    size_t n = 0; while (name[n]) n++;
    if (n + 1 + vlen > cap) return 0;
    for (size_t i = 0; i <= n; i++) o[i] = (uint8_t)name[i];
    const uint8_t *v = (const uint8_t *)val;
    for (size_t i = 0; i < vlen; i++) o[n + 1 + i] = v ? v[i] : 0;
    return n + 1 + vlen;
}

size_t brcm_ssid(uint8_t *o, const uint8_t *ssid, size_t len)
{
    if (len > 32) len = 32;
    zero(o, 36); w32(o, (uint32_t)len);
    for (size_t i = 0; i < len; i++) o[4 + i] = ssid[i];
    return 36;
}

size_t brcm_escan_params(uint8_t *o, uint16_t sync_id, const uint8_t *ssid, size_t ssid_len)
{
    zero(o, ESCAN_PARAMS_LEN);
    w32(o, 1); w16(o + 4, 1); w16(o + 6, sync_id);
    uint8_t *p = o + 8;
    if (ssid) brcm_ssid(p, ssid, ssid_len);
    for (int i = 0; i < 6; i++) p[36 + i] = 0xFF;                   /* cualquier BSSID */
    p[42] = 2;                                                      /* cualquier tipo de red */
    p[43] = 0;                                                      /* escaneo activo */
    w32(p + 44, 0xFFFFFFFFu); w32(p + 48, 0xFFFFFFFFu); w32(p + 52, 0xFFFFFFFFu); w32(p + 56, 0xFFFFFFFFu);   /* tiempos por defecto */
    w32(p + 60, 0);                                                 /* todos los canales */
    return ESCAN_PARAMS_LEN;
}

size_t brcm_wsec_key(uint8_t *o, uint32_t index, const uint8_t *key, uint32_t klen, uint32_t algo, int primary, const uint8_t ea[6])
{
    zero(o, WSEC_KEY_LEN);
    if (klen > 32) klen = 32;
    w32(o, index); w32(o + 4, klen);
    for (uint32_t i = 0; i < klen; i++) o[8 + i] = key[i];
    w32(o + 112, algo); w32(o + 116, primary ? 2u : 0u);
    if (ea) for (int i = 0; i < 6; i++) o[156 + i] = ea[i];
    return WSEC_KEY_LEN;
}

size_t brcm_clm_chunk(uint8_t *o, const uint8_t *data, uint16_t n, int first, int last)
{
    w16(o, (uint16_t)((1u << 12) | (first ? 2u : 0u) | (last ? 4u : 0u)));
    w16(o + 2, 2); w32(o + 4, n); w32(o + 8, 0);
    for (uint16_t i = 0; i < n; i++) o[12 + i] = data[i];
    return 12u + n;
}

void brcm_event_mask(uint8_t m[16], const uint32_t *ev, int n)
{
    zero(m, 16);
    for (int i = 0; i < n; i++) if (ev[i] < 128) m[ev[i] / 8] |= (uint8_t)(1u << (ev[i] % 8));
}

int brcm_event_parse(const uint8_t *b, size_t len, struct brcm_event *o)
{
    if (len < 72 || b16(b + 12) != 0x886C) return -1;
    const uint8_t *h = b + 14;
    if (b16(h) != 0x8001 || h[5] != 0x00 || h[6] != 0x10 || h[7] != 0x18) return -1;
    const uint8_t *e = h + 10;
    o->flags = b16(e + 2); o->type = b32(e + 4); o->status = b32(e + 8); o->reason = b32(e + 12); o->auth = b32(e + 16); o->datalen = b32(e + 20);
    for (int i = 0; i < 6; i++) o->addr[i] = e[24 + i];
    if (o->datalen > len - 72) return -2;
    o->data = b + 72;
    return 0;
}

const char *brcm_event_name(uint32_t t)
{
    switch (t) {
    case BRCM_E_SET_SSID: return "SET_SSID"; case BRCM_E_JOIN: return "JOIN"; case BRCM_E_AUTH: return "AUTH";
    case BRCM_E_DEAUTH: return "DEAUTH"; case BRCM_E_DEAUTH_IND: return "DEAUTH_IND"; case BRCM_E_ASSOC: return "ASSOC";
    case BRCM_E_DISASSOC: return "DISASSOC"; case BRCM_E_DISASSOC_IND: return "DISASSOC_IND"; case BRCM_E_LINK: return "LINK";
    case BRCM_E_PSK_SUP: return "PSK_SUP"; case BRCM_E_ESCAN_RESULT: return "ESCAN_RESULT";
    default: return "?";
    }
}
