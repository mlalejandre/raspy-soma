/* PARCHE 083: PNG + zlib/deflate. Ver png_enc.h. */
#include "png_enc.h"

uint32_t png_crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++) { crc ^= p[i]; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); }
    return ~crc;
}
uint32_t png_adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    while (n) { size_t k = n < 5552 ? n : 5552; n -= k; while (k--) { a += *p++; b += a; } a %= 65521u; b %= 65521u; }
    return (b << 16) | a;
}

/* ---- escritor de bits ---- */
struct bw { uint8_t *o; size_t cap, n; uint32_t acc; int nb; int full; };
static void put(struct bw *w, uint32_t v, int bits)                 /* LSB primero */
{
    w->acc |= v << w->nb; w->nb += bits;
    while (w->nb >= 8) { if (w->n < w->cap) w->o[w->n++] = (uint8_t)w->acc; else w->full = 1; w->acc >>= 8; w->nb -= 8; }
}
static void put_rev(struct bw *w, uint32_t code, int len)            /* codigos Huffman: MSB primero */
{
    uint32_t r = 0; for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    put(w, r, len);
}
static void lit(struct bw *w, int c)
{
    if (c < 144) put_rev(w, 0x30u + (uint32_t)c, 8);
    else if (c < 256) put_rev(w, 0x190u + (uint32_t)(c - 144), 9);
    else if (c < 280) put_rev(w, (uint32_t)(c - 256), 7);
    else put_rev(w, 0xC0u + (uint32_t)(c - 280), 8);
}
static const uint16_t LBASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint8_t  LEXT[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t DBASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint8_t  DEXT[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static void match(struct bw *w, int len, int dist)
{
    int i = 28; while (LBASE[i] > len) i--;
    lit(w, 257 + i); if (LEXT[i]) put(w, (uint32_t)(len - LBASE[i]), LEXT[i]);
    int d = 29; while (DBASE[d] > dist) d--;
    put_rev(w, (uint32_t)d, 5); if (DEXT[d]) put(w, (uint32_t)(dist - DBASE[d]), DEXT[d]);
}

#define HBITS 15
#define HSIZE (1u << HBITS)
#define WIN   32768
size_t deflate_work_size(size_t n) { (void)n; return (HSIZE + WIN) * sizeof(int32_t); }

size_t zlib_compress(const uint8_t *in, size_t n, uint8_t *out, size_t cap, void *work)
{
    if (cap < 8) return 0;
    struct bw w = { out, cap, 0, 0, 0, 0 };
    out[0] = 0x78; out[1] = 0x01; w.n = 2;
    int32_t *head = (int32_t *)work, *prev = head + HSIZE;
    for (uint32_t i = 0; i < HSIZE; i++) head[i] = -1;
    put(&w, 1, 1); put(&w, 1, 2);                                    /* un solo bloque final con Huffman fijo */
    size_t i = 0;
    while (i < n) {
        int best = 0, bdist = 0;
        if (i + 3 <= n) {
            uint32_t h = ((uint32_t)in[i] << 10 ^ (uint32_t)in[i + 1] << 5 ^ in[i + 2]) & (HSIZE - 1);
            int32_t c = head[h]; int chain = 48;
            size_t maxl = n - i < 258 ? n - i : 258;
            while (c >= 0 && i - (size_t)c <= WIN && chain--) {
                const uint8_t *a = in + c, *b = in + i;
                if (a[best] == b[best]) { size_t l = 0; while (l < maxl && a[l] == b[l]) l++; if ((int)l > best) { best = (int)l; bdist = (int)(i - (size_t)c); if (l == maxl) break; } }
                int32_t nc = prev[c & (WIN - 1)];
                if (nc >= c) break;                                      /* entrada ya reutilizada por la ventana */
                c = nc;
            }
            prev[i & (WIN - 1)] = head[h]; head[h] = (int32_t)i;
        }
        if (best >= 3) {
            match(&w, best, bdist);
            for (size_t k = 1; k < (size_t)best; k++) {                 /* indexa las posiciones saltadas */
                size_t j = i + k;
                if (j + 3 <= n) { uint32_t h = ((uint32_t)in[j] << 10 ^ (uint32_t)in[j + 1] << 5 ^ in[j + 2]) & (HSIZE - 1); prev[j & (WIN - 1)] = head[h]; head[h] = (int32_t)j; }
            }
            i += (size_t)best;
        } else { lit(&w, in[i]); i++; }
        if (w.full) return 0;
    }
    lit(&w, 256);
    if (w.nb) put(&w, 0, 8 - w.nb);
    uint32_t ad = png_adler32(in, n);
    uint8_t t[4] = { (uint8_t)(ad >> 24), (uint8_t)(ad >> 16), (uint8_t)(ad >> 8), (uint8_t)ad };
    for (int k = 0; k < 4; k++) { if (w.n >= cap) return 0; out[w.n++] = t[k]; }
    return w.full ? 0 : w.n;
}

size_t png_work_size(int w, int h)
{
    size_t raw = ((size_t)h * ((size_t)w * 3 + 1) + 15) & ~(size_t)15;
    return raw + deflate_work_size(raw) + (size_t)w * 3 * 3;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

size_t png_encode_rgb(const uint32_t *px, int w, int h, uint8_t *out, size_t cap, void *work)
{
    if (w <= 0 || h <= 0 || cap < 67) return 0;
    size_t stride = (size_t)w * 3, raw_n = (size_t)h * (stride + 1);
    size_t raw_al = (raw_n + 15) & ~(size_t)15;
    uint8_t *raw = (uint8_t *)work, *cand = raw + raw_al + deflate_work_size(raw_al), *prevrow = cand + stride, *cur = prevrow + stride;
    void *dwork = raw + raw_al;
    for (size_t k = 0; k < stride; k++) prevrow[k] = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) { uint32_t c = px[(size_t)y * (size_t)w + (size_t)x]; cur[3 * x] = (uint8_t)(c >> 16); cur[3 * x + 1] = (uint8_t)(c >> 8); cur[3 * x + 2] = (uint8_t)c; }
        /* filtro: el de menor suma de valores absolutos (con signo) */
        uint32_t best = 0xFFFFFFFFu; int bf = 0;
        for (int f = 0; f < 3; f++) {
            uint32_t s = 0;
            for (size_t k = 0; k < stride; k++) {
                uint8_t a = k >= 3 ? cur[k - 3] : 0, v = f == 0 ? cur[k] : f == 1 ? (uint8_t)(cur[k] - a) : (uint8_t)(cur[k] - prevrow[k]);
                s += v < 128 ? v : 256u - v;
            }
            if (s < best) { best = s; bf = f; }
        }
        uint8_t *row = raw + (size_t)y * (stride + 1);
        row[0] = (uint8_t)(bf == 0 ? 0 : bf == 1 ? 1 : 2);
        for (size_t k = 0; k < stride; k++) { uint8_t a = k >= 3 ? cur[k - 3] : 0; row[1 + k] = bf == 0 ? cur[k] : bf == 1 ? (uint8_t)(cur[k] - a) : (uint8_t)(cur[k] - prevrow[k]); }
        for (size_t k = 0; k < stride; k++) prevrow[k] = cur[k];
    }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    size_t o = 0;
    for (int k = 0; k < 8; k++) out[o++] = sig[k];
    /* IHDR */
    be32(out + o, 13); o += 4;
    uint8_t *ck = out + o;
    out[o++] = 'I'; out[o++] = 'H'; out[o++] = 'D'; out[o++] = 'R';
    be32(out + o, (uint32_t)w); o += 4; be32(out + o, (uint32_t)h); o += 4;
    out[o++] = 8; out[o++] = 2; out[o++] = 0; out[o++] = 0; out[o++] = 0;
    be32(out + o, png_crc32(0, ck, 17)); o += 4;
    /* IDAT */
    if (o + 12 + 12 > cap) return 0;
    size_t zn = zlib_compress(raw, raw_n, out + o + 8, cap - o - 8 - 4 - 12, dwork);
    if (!zn) return 0;
    be32(out + o, (uint32_t)zn); o += 4;
    ck = out + o; out[o++] = 'I'; out[o++] = 'D'; out[o++] = 'A'; out[o++] = 'T';
    o += zn;
    be32(out + o, png_crc32(0, ck, zn + 4)); o += 4;
    /* IEND */
    be32(out + o, 0); o += 4;
    ck = out + o; out[o++] = 'I'; out[o++] = 'E'; out[o++] = 'N'; out[o++] = 'D';
    be32(out + o, png_crc32(0, ck, 4)); o += 4;
    return o;
}
