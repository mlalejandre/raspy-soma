/* SOMA_PARCHE_022_VIDEO: ver jpeg.h */
#include "jpeg.h"

#define MAXCOMP 3
#define MAXTAB 4

struct huff {
    int defined;
    uint8_t bits[17];                /* numero de codigos de cada longitud 1..16 */
    uint8_t vals[256];
    int32_t maxcode[18];             /* mayor codigo de cada longitud (-1 si no hay) */
    int32_t valptr[17];
    int32_t mincode[17];
    int16_t look[512];               /* 9 bits: (longitud << 8) | simbolo, o -1 */
};

struct comp {
    uint8_t id, h, v, tq, td, ta;
    int32_t pred;
};

struct dec {
    const uint8_t *d; size_t len, pos;
    uint16_t qt[MAXTAB][64]; int qdef[MAXTAB];
    struct huff dc[MAXTAB], ac[MAXTAB];
    struct comp c[MAXCOMP];
    struct jpeg_info info;
    int have_sof, nscan_comp;
    uint32_t mcux, mcuy;
    /* lector de bits */
    uint32_t bitbuf; int bitcnt; int hit_marker; int eof;
    uint16_t restart_interval;
};

static const uint8_t ZZ[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

const char *jpeg_strerror(int c)
{
    switch (c) {
    case JPEG_OK: return "ok";
    case JPEG_E_FORMAT: return "no es un JPEG";
    case JPEG_E_UNSUPPORTED: return "JPEG no soportado (solo baseline secuencial, 8 bits, entrelazado)";
    case JPEG_E_DATA: return "datos JPEG corruptos";
    case JPEG_E_TRUNC: return "JPEG truncado";
    case JPEG_E_SIZE: return "dimensiones JPEG fuera de rango";
    default: return "error desconocido";
    }
}

static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }

/* ---------------------------------------------------------------- tablas de Huffman */
static int build_huff(struct huff *h)
{
    int code = 0, k = 0;
    for (int i = 0; i < 512; ++i) h->look[i] = -1;
    for (int l = 1; l <= 16; ++l) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        if (code > (1 << l)) return -1;                        /* mas codigos de los posibles: tabla invalida */
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        if (l <= 9) {                                         /* atajo de 9 bits */
            for (int i = 0; i < h->bits[l]; ++i) {
                int c = h->mincode[l] + i;
                int sym = h->vals[h->valptr[l] + i];
                int base = c << (9 - l);
                for (int j = 0; j < (1 << (9 - l)); ++j) h->look[base + j] = (int16_t)((l << 8) | sym);
            }
        }
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
    return 0;
}

/* ---------------------------------------------------------------- lector de bits */
static void fill(struct dec *s)
{
    while (s->bitcnt <= 24) {
        uint32_t b = 0;
        if (!s->hit_marker) {
            if (s->pos >= s->len) { s->eof = 1; s->hit_marker = 1; }
            else {
                b = s->d[s->pos];
                if (b == 0xFF) {
                    if (s->pos + 1 >= s->len) { s->eof = 1; s->hit_marker = 1; b = 0; }
                    else if (s->d[s->pos + 1] == 0x00) { s->pos += 2; }       /* byte 0xFF "rellenado" */
                    else { s->hit_marker = 1; b = 0; }                          /* marcador: no se consume */
                } else s->pos++;
            }
        }
        s->bitbuf |= b << (24 - s->bitcnt);
        s->bitcnt += 8;
    }
}

static inline uint32_t peek(struct dec *s, int n) { if (s->bitcnt < n) fill(s); return s->bitbuf >> (32 - n); }
static inline void skip(struct dec *s, int n) { s->bitbuf <<= n; s->bitcnt -= n; }
static inline int32_t getbits(struct dec *s, int n)
{
    if (n == 0) return 0;
    uint32_t v = peek(s, n); skip(s, n); return (int32_t)v;
}

static int decode_sym(struct dec *s, const struct huff *h)
{
    if (s->bitcnt < 16) fill(s);
    int16_t e = h->look[s->bitbuf >> 23];
    if (e >= 0) { skip(s, e >> 8); return e & 0xFF; }
    int l = 10;
    int32_t code = (int32_t)(s->bitbuf >> 22);                  /* 10 bits */
    for (; l <= 16; ++l, code = (int32_t)(s->bitbuf >> (32 - l)))
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) break;
    if (l > 16) return -1;
    skip(s, l);
    int idx = h->valptr[l] + (code - h->mincode[l]);
    return idx >= 0 && idx < 256 ? h->vals[idx] : -1;
}

static inline int32_t extend(int32_t v, int n) { return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

/* ---------------------------------------------------------------- bloque 8x8 */
static int decode_block(struct dec *s, struct comp *c, int16_t *blk)
{
    for (int i = 0; i < 64; ++i) blk[i] = 0;
    int t = decode_sym(s, &s->dc[c->td]);
    if (t < 0 || t > 15) return JPEG_E_DATA;
    int32_t diff = t ? extend(getbits(s, t), t) : 0;
    c->pred += diff;
    blk[0] = (int16_t)c->pred;
    const struct huff *ac = &s->ac[c->ta];
    for (int k = 1; k < 64; ) {
        int rs = decode_sym(s, ac);
        if (rs < 0) return JPEG_E_DATA;
        int r = rs >> 4, sz = rs & 15;
        if (sz == 0) {
            if (r == 15) { k += 16; continue; }              /* ZRL */
            break;                                            /* EOB */
        }
        k += r;
        if (k > 63) return JPEG_E_DATA;
        blk[ZZ[k]] = (int16_t)extend(getbits(s, sz), sz);
        k++;
    }
    if (s->eof && s->bitcnt < -8) return JPEG_E_TRUNC;
    return 0;
}

/* ---------------------------------------------------------------- IDCT (jidctint de libjpeg, 13 bits) */
#define CONST_BITS 13
#define PASS1_BITS 2
#define F_0_298631336 2446
#define F_0_390180644 3196
#define F_0_541196100 4433
#define F_0_765366865 6270
#define F_0_899976223 7373
#define F_1_175875602 9633
#define F_1_501321110 12299
#define F_1_847759065 15137
#define F_1_961570560 16069
#define F_2_053119869 16819
#define F_2_562915447 20995
#define F_3_072711026 25172
#define DESCALE(x, n) (((x) + ((int64_t)1 << ((n) - 1))) >> (n))

static inline uint8_t clamp8(int64_t v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void idct(const int16_t *in, const uint16_t *q, uint8_t *out, int stride)
{
    int64_t ws[64];
    for (int c = 0; c < 8; ++c) {
        const int16_t *p = in + c; const uint16_t *qq = q + c; int64_t *w = ws + c;
        if (!p[8] && !p[16] && !p[24] && !p[32] && !p[40] && !p[48] && !p[56]) {
            int64_t dc = ((int64_t)p[0] * qq[0]) * (1 << PASS1_BITS);
            for (int r = 0; r < 8; ++r) w[r * 8] = dc;
            continue;
        }
        int64_t z2 = (int64_t)p[16] * qq[16], z3 = (int64_t)p[48] * qq[48];
        int64_t z1 = (z2 + z3) * F_0_541196100;
        int64_t tmp2 = z1 + z3 * -F_1_847759065, tmp3 = z1 + z2 * F_0_765366865;
        z2 = (int64_t)p[0] * qq[0]; z3 = (int64_t)p[32] * qq[32];
        int64_t tmp0 = (z2 + z3) * (1 << CONST_BITS), tmp1 = (z2 - z3) * (1 << CONST_BITS);
        int64_t t10 = tmp0 + tmp3, t13 = tmp0 - tmp3, t11 = tmp1 + tmp2, t12 = tmp1 - tmp2;
        tmp0 = (int64_t)p[56] * qq[56]; tmp1 = (int64_t)p[40] * qq[40]; tmp2 = (int64_t)p[24] * qq[24]; tmp3 = (int64_t)p[8] * qq[8];
        z1 = tmp0 + tmp3; z2 = tmp1 + tmp2; z3 = tmp0 + tmp2; int64_t z4 = tmp1 + tmp3; int64_t z5 = (z3 + z4) * F_1_175875602;
        tmp0 *= F_0_298631336; tmp1 *= F_2_053119869; tmp2 *= F_3_072711026; tmp3 *= F_1_501321110;
        z1 *= -F_0_899976223; z2 *= -F_2_562915447; z3 *= -F_1_961570560; z4 *= -F_0_390180644;
        z3 += z5; z4 += z5;
        tmp0 += z1 + z3; tmp1 += z2 + z4; tmp2 += z2 + z3; tmp3 += z1 + z4;
        w[0] = DESCALE(t10 + tmp3, CONST_BITS - PASS1_BITS); w[56] = DESCALE(t10 - tmp3, CONST_BITS - PASS1_BITS);
        w[8] = DESCALE(t11 + tmp2, CONST_BITS - PASS1_BITS); w[48] = DESCALE(t11 - tmp2, CONST_BITS - PASS1_BITS);
        w[16] = DESCALE(t12 + tmp1, CONST_BITS - PASS1_BITS); w[40] = DESCALE(t12 - tmp1, CONST_BITS - PASS1_BITS);
        w[24] = DESCALE(t13 + tmp0, CONST_BITS - PASS1_BITS); w[32] = DESCALE(t13 - tmp0, CONST_BITS - PASS1_BITS);
    }
    for (int r = 0; r < 8; ++r) {
        const int64_t *w = ws + r * 8; uint8_t *o = out + r * stride;
        if (!w[1] && !w[2] && !w[3] && !w[4] && !w[5] && !w[6] && !w[7]) {
            uint8_t dc = clamp8(DESCALE(w[0], PASS1_BITS + 3) + 128);
            for (int x = 0; x < 8; ++x) o[x] = dc;
            continue;
        }
        int64_t z2 = w[2], z3 = w[6];
        int64_t z1 = (z2 + z3) * F_0_541196100;
        int64_t tmp2 = z1 + z3 * -F_1_847759065, tmp3 = z1 + z2 * F_0_765366865;
        int64_t tmp0 = (w[0] + w[4]) * (1 << CONST_BITS), tmp1 = (w[0] - w[4]) * (1 << CONST_BITS);
        int64_t t10 = tmp0 + tmp3, t13 = tmp0 - tmp3, t11 = tmp1 + tmp2, t12 = tmp1 - tmp2;
        tmp0 = w[7]; tmp1 = w[5]; tmp2 = w[3]; tmp3 = w[1];
        z1 = tmp0 + tmp3; z2 = tmp1 + tmp2; z3 = tmp0 + tmp2; int64_t z4 = tmp1 + tmp3; int64_t z5 = (z3 + z4) * F_1_175875602;
        tmp0 *= F_0_298631336; tmp1 *= F_2_053119869; tmp2 *= F_3_072711026; tmp3 *= F_1_501321110;
        z1 *= -F_0_899976223; z2 *= -F_2_562915447; z3 *= -F_1_961570560; z4 *= -F_0_390180644;
        z3 += z5; z4 += z5;
        tmp0 += z1 + z3; tmp1 += z2 + z4; tmp2 += z2 + z3; tmp3 += z1 + z4;
        const int sh = CONST_BITS + PASS1_BITS + 3;
        o[0] = clamp8(DESCALE(t10 + tmp3, sh) + 128); o[7] = clamp8(DESCALE(t10 - tmp3, sh) + 128);
        o[1] = clamp8(DESCALE(t11 + tmp2, sh) + 128); o[6] = clamp8(DESCALE(t11 - tmp2, sh) + 128);
        o[2] = clamp8(DESCALE(t12 + tmp1, sh) + 128); o[5] = clamp8(DESCALE(t12 - tmp1, sh) + 128);
        o[3] = clamp8(DESCALE(t13 + tmp0, sh) + 128); o[4] = clamp8(DESCALE(t13 - tmp0, sh) + 128);
    }
}

/* ---------------------------------------------------------------- cabeceras */
static int parse_headers(struct dec *s, int stop_at_sos)
{
    const uint8_t *d = s->d; size_t len = s->len;
    if (len < 4 || d[0] != 0xFF || d[1] != 0xD8) return JPEG_E_FORMAT;
    s->pos = 2;
    for (;;) {
        /* siguiente marcador */
        while (s->pos < len && d[s->pos] != 0xFF) s->pos++;
        while (s->pos < len && d[s->pos] == 0xFF) s->pos++;
        if (s->pos >= len) return JPEG_E_TRUNC;
        uint8_t m = d[s->pos++];
        if (m == 0x00 || m == 0x01 || (m >= 0xD0 && m <= 0xD8)) continue;     /* sin longitud */
        if (m == 0xD9) return JPEG_E_FORMAT;                                    /* EOI antes de SOS */
        if (s->pos + 2 > len) return JPEG_E_TRUNC;
        size_t seg = be16(d + s->pos);
        if (seg < 2 || s->pos + seg > len) return JPEG_E_TRUNC;
        const uint8_t *p = d + s->pos + 2; size_t n = seg - 2;
        size_t next = s->pos + seg;

        if (m == 0xDB) {                                                         /* DQT */
            while (n > 0) {
                int pq = p[0] >> 4, tq = p[0] & 15; p++; n--;
                if (tq >= MAXTAB || pq > 1) return JPEG_E_DATA;
                size_t need = pq ? 128 : 64;
                if (n < need) return JPEG_E_DATA;
                for (int i = 0; i < 64; ++i) {
                    uint16_t v = pq ? (uint16_t)be16(p + 2 * i) : p[i];
                    s->qt[tq][ZZ[i]] = v;
                }
                s->qdef[tq] = 1; p += need; n -= need;
            }
        } else if (m == 0xC4) {                                                  /* DHT */
            while (n > 0) {
                if (n < 17) return JPEG_E_DATA;
                int tc = p[0] >> 4, th = p[0] & 15;
                if (tc > 1 || th >= MAXTAB) return JPEG_E_DATA;
                struct huff *h = tc ? &s->ac[th] : &s->dc[th];
                int total = 0;
                for (int i = 1; i <= 16; ++i) { h->bits[i] = p[i]; total += p[i]; }
                p += 17; n -= 17;
                if (total > 256 || (size_t)total > n) return JPEG_E_DATA;
                for (int i = 0; i < total; ++i) h->vals[i] = p[i];
                for (int i = total; i < 256; ++i) h->vals[i] = 0;
                p += total; n -= (size_t)total;
                if (build_huff(h) != 0) return JPEG_E_DATA;
                h->defined = 1;
            }
        } else if (m == 0xC0 || m == 0xC1) {                                      /* SOF0 / SOF1 */
            if (s->have_sof || n < 6) return JPEG_E_DATA;
            if (p[0] != 8) return JPEG_E_UNSUPPORTED;                              /* solo 8 bits */
            uint32_t h = be16(p + 1), w = be16(p + 3);
            int nc = p[5];
            if (w == 0 || h == 0 || w > JPEG_MAX_DIM || h > JPEG_MAX_DIM) return JPEG_E_SIZE;
            if ((nc != 1 && nc != 3) || n < 6 + (size_t)nc * 3) return nc == 1 || nc == 3 ? JPEG_E_DATA : JPEG_E_UNSUPPORTED;
            for (int i = 0; i < nc; ++i) {
                struct comp *c = &s->c[i];
                c->id = p[6 + i * 3]; c->h = p[7 + i * 3] >> 4; c->v = p[7 + i * 3] & 15; c->tq = p[8 + i * 3];
                if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2 || c->tq >= MAXTAB) return JPEG_E_UNSUPPORTED;
            }
            s->info.width = w; s->info.height = h; s->info.ncomp = (uint8_t)nc;
            s->info.hmax = 1; s->info.vmax = 1;
            if (nc == 3) for (int i = 0; i < 3; ++i) { if (s->c[i].h > s->info.hmax) s->info.hmax = s->c[i].h; if (s->c[i].v > s->info.vmax) s->info.vmax = s->c[i].v; }
            s->have_sof = 1;
        } else if ((m >= 0xC2 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            return JPEG_E_UNSUPPORTED;                                            /* progresivo, aritmetico, sin perdidas... */
        } else if (m == 0xDD) {                                                   /* DRI */
            if (n < 2) return JPEG_E_DATA;
            s->restart_interval = (uint16_t)be16(p);
            s->info.restart_interval = s->restart_interval;
        } else if (m == 0xDA) {                                                   /* SOS */
            if (!s->have_sof) return JPEG_E_DATA;
            if (n < 1) return JPEG_E_DATA;
            int ns = p[0];
            if (ns != s->info.ncomp) return JPEG_E_UNSUPPORTED;                   /* solo escaneo unico entrelazado */
            if (n < 1 + (size_t)ns * 2 + 3) return JPEG_E_DATA;
            for (int i = 0; i < ns; ++i) {
                int id = p[1 + i * 2], tt = p[2 + i * 2];
                int found = -1;
                for (int k = 0; k < ns; ++k) if (s->c[k].id == id) found = k;
                if (found < 0 || found != i) return JPEG_E_DATA;                  /* el orden debe ser el del SOF */
                s->c[i].td = (uint8_t)(tt >> 4); s->c[i].ta = (uint8_t)(tt & 15);
                if (s->c[i].td >= MAXTAB || s->c[i].ta >= MAXTAB) return JPEG_E_DATA;
            }
            const uint8_t *e = p + 1 + ns * 2;
            if (e[0] != 0 || e[1] != 63 || e[2] != 0) return JPEG_E_UNSUPPORTED;  /* Ss/Se/Ah/Al de baseline */
            s->pos = next;
            return JPEG_OK;
        }
        s->pos = next;
        (void)stop_at_sos;
    }
}

int jpeg_get_info(const uint8_t *data, size_t len, struct jpeg_info *info)
{
    struct dec s;
    uint8_t *z = (uint8_t *)&s; for (size_t i = 0; i < sizeof s; ++i) z[i] = 0;
    s.d = data; s.len = len;
    int r = parse_headers(&s, 1);
    if (r == JPEG_OK && info) *info = s.info;
    return r;
}

/* ---------------------------------------------------------------- decodificacion */
static int restart(struct dec *s)
{
    /* alinea a byte, busca RSTn y reinicia predictores y lector */
    s->bitbuf = 0; s->bitcnt = 0;
    size_t p = s->pos;
    while (p + 1 < s->len && !(s->d[p] == 0xFF && s->d[p + 1] >= 0xD0 && s->d[p + 1] <= 0xD7)) {
        if (s->d[p] == 0xFF && s->d[p + 1] != 0x00 && s->d[p + 1] != 0xFF) return JPEG_E_DATA;   /* otro marcador */
        p++;
    }
    if (p + 1 >= s->len) return JPEG_E_TRUNC;
    s->pos = p + 2; s->hit_marker = 0; s->eof = 0;
    for (int i = 0; i < MAXCOMP; ++i) s->c[i].pred = 0;
    return 0;
}

/* SOMA_PARCHE_028_UI: el navegador decodifica en su hilo y cede la CPU entre filas de MCU (planificador cooperativo).
 * Definicion debil vacia: el reproductor y las pruebas no la necesitan. */
__attribute__((weak)) void jpeg_yield_hook(void) {}

int jpeg_decode(const uint8_t *data, size_t len, uint32_t *out, uint32_t out_stride, uint32_t out_max_height, struct jpeg_info *info)
{
    struct dec s;
    uint8_t *z = (uint8_t *)&s; for (size_t i = 0; i < sizeof s; ++i) z[i] = 0;
    s.d = data; s.len = len;
    int r = parse_headers(&s, 0);
    if (r != JPEG_OK) return r;
    if (info) *info = s.info;
    const uint32_t W = s.info.width, H = s.info.height;
    if (!out || out_stride < W || out_max_height < H) return JPEG_E_SIZE;
    const int nc = s.info.ncomp;
    for (int i = 0; i < nc; ++i) {
        if (!s.qdef[s.c[i].tq] || !s.dc[s.c[i].td].defined || !s.ac[s.c[i].ta].defined) return JPEG_E_DATA;
    }
    const int hmax = s.info.hmax, vmax = s.info.vmax;
    const uint32_t mcu_w = (nc == 1 ? 8u : 8u * (uint32_t)hmax), mcu_h = (nc == 1 ? 8u : 8u * (uint32_t)vmax);
    s.mcux = (W + mcu_w - 1) / mcu_w; s.mcuy = (H + mcu_h - 1) / mcu_h;

    uint8_t samp[MAXCOMP][16 * 16];                              /* muestras de un MCU por componente */
    int16_t blk[64];
    uint32_t restart_count = 0;
    for (uint32_t my = 0; my < s.mcuy; ++my) {
        jpeg_yield_hook();                                       /* SOMA_PARCHE_028_UI: una fila de MCU por vez */
        for (uint32_t mx = 0; mx < s.mcux; ++mx) {
            if (s.restart_interval && restart_count == s.restart_interval) {
                int rr = restart(&s); if (rr) return rr;
                restart_count = 0;
            }
            restart_count++;
            for (int ci = 0; ci < nc; ++ci) {
                struct comp *c = &s.c[ci];
                int bh = nc == 1 ? 1 : c->h, bv = nc == 1 ? 1 : c->v;
                for (int by = 0; by < bv; ++by)
                    for (int bx = 0; bx < bh; ++bx) {
                        int rr = decode_block(&s, c, blk);
                        if (rr) return rr;
                        int stride = bh * 8;
                        idct(blk, s.qt[c->tq], &samp[ci][by * 8 * stride + bx * 8], stride);
                    }
            }
            /* a pixeles */
            uint32_t x0 = mx * mcu_w, y0 = my * mcu_h;
            for (uint32_t py = 0; py < mcu_h && y0 + py < H; ++py) {
                uint32_t *dst = out + (size_t)(y0 + py) * out_stride + x0;
                for (uint32_t px = 0; px < mcu_w && x0 + px < W; ++px) {
                    int Y, Cb, Cr;
                    if (nc == 1) {
                        Y = samp[0][py * 8 + px];
                        dst[px] = ((uint32_t)Y << 16) | ((uint32_t)Y << 8) | (uint32_t)Y;
                    } else {
                        /* cada componente se lee en su propia rejilla: la muestra de (px, py) del MCU es la (px*h/hmax, py*v/vmax) */
                        Y  = samp[0][(py * s.c[0].v / vmax) * (8 * s.c[0].h) + px * s.c[0].h / hmax];
                        Cb = samp[1][(py * s.c[1].v / vmax) * (8 * s.c[1].h) + px * s.c[1].h / hmax];
                        Cr = samp[2][(py * s.c[2].v / vmax) * (8 * s.c[2].h) + px * s.c[2].h / hmax];
                        int32_t cb = Cb - 128, cr = Cr - 128;
                        int32_t R = Y + ((91881 * cr + 32768) >> 16);
                        int32_t G = Y + ((-22554 * cb - 46802 * cr + 32768) >> 16);
                        int32_t B = Y + ((116130 * cb + 32768) >> 16);
                        dst[px] = ((uint32_t)clamp8(R) << 16) | ((uint32_t)clamp8(G) << 8) | (uint32_t)clamp8(B);
                    }
                }
            }
        }
    }
    return JPEG_OK;
}
