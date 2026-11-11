/* SOMA_PARCHE_NAVEGADOR: descompresion DEFLATE / zlib / gzip (RFC 1951/1950/1952), solo enteros. */
#include "web_inflate.h"

struct wzh { uint16_t count[16]; uint16_t symbol[288]; uint16_t fast[1024]; };   /* fast: (simbolo << 4) | longitud */
struct wzs { const uint8_t *in; uint32_t n, pos; uint32_t bitbuf; int bitcnt; struct wb *out; uint32_t max; int err; };

static void refill(struct wzs *S) { while (S->bitcnt <= 24 && S->pos < S->n) { S->bitbuf |= (uint32_t)S->in[S->pos++] << S->bitcnt; S->bitcnt += 8; } }
static uint32_t getbits(struct wzs *S, int need)
{
    if (need == 0) return 0;
    refill(S);
    if (S->bitcnt < need) { S->err = 1; return 0; }
    uint32_t v = S->bitbuf & ((1u << need) - 1u); S->bitbuf >>= need; S->bitcnt -= need; return v;
}
static int build(struct wzh *h, const uint8_t *len, int n)
{
    memset(h->count, 0, sizeof h->count); memset(h->fast, 0, sizeof h->fast);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1; for (int l = 1; l < 16; l++) { left <<= 1; left -= h->count[l]; if (left < 0) return -1; }
    uint16_t offs[16]; offs[1] = 0; for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++) if (len[i]) h->symbol[offs[len[i]]++] = (uint16_t)i;
    /* tabla rapida para codigos <= 10 bits */
    uint32_t code = 0; int idx = 0;
    for (int l = 1; l <= 15; l++) {
        for (int k = 0; k < h->count[l]; k++, idx++, code++) {
            if (l <= 10) {
                uint32_t rev = 0; for (int b = 0; b < l; b++) if (code & (1u << b)) rev |= 1u << (l - 1 - b);
                for (uint32_t f = rev; f < 1024; f += (1u << l)) h->fast[f] = (uint16_t)((h->symbol[idx] << 4) | l);
            }
        }
        code <<= 1;
    }
    return left;
}
static int decode(struct wzs *S, const struct wzh *h)
{
    refill(S);
    uint16_t f = h->fast[S->bitbuf & 1023];
    if (f && (f & 15) <= S->bitcnt) { int l = f & 15; S->bitbuf >>= l; S->bitcnt -= l; return f >> 4; }
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= 15; l++) {
        if (S->bitcnt <= 0) { refill(S); if (S->bitcnt <= 0) { S->err = 1; return -1; } }
        code |= (int)(S->bitbuf & 1); S->bitbuf >>= 1; S->bitcnt--;
        int cnt = h->count[l];
        if (code - cnt < first) return h->symbol[index + (code - first)];
        index += cnt; first += cnt; first <<= 1; code <<= 1;
    }
    S->err = 1; return -1;
}
static const uint16_t lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(struct wzs *S, const struct wzh *lh, const struct wzh *dh)
{
    struct wb *o = S->out; uint32_t tk = 0;
    for (;;) {
        if ((++tk & 1023u) == 0) wu_tick();          /* SOMA_PARCHE_028_UI */
        int sym = decode(S, lh); if (sym < 0 || S->err) return -1;
        if (sym < 256) {
            if (!wb_reserve(o, 1) || o->len >= S->max) return -2;
            o->p[o->len++] = (char)sym; continue;
        }
        if (sym == 256) { if (o->p) o->p[o->len] = 0; return 0; }
        sym -= 257; if (sym >= 29) return -1;
        int len = lbase[sym] + (int)getbits(S, lext[sym]); if (S->err) return -1;
        int ds = decode(S, dh); if (ds < 0 || ds >= 30) return -1;
        uint32_t dist = dbase[ds] + getbits(S, dext[ds]); if (S->err) return -1;
        if (dist > o->len) return -1;
        if (o->len + (uint32_t)len > S->max) return -2;
        if (!wb_reserve(o, (uint32_t)len)) return -2;
        char *d = o->p + o->len; const char *s = d - dist;
        for (int i = 0; i < len; i++) d[i] = s[i];
        o->len += (uint32_t)len;
    }
}
static int stored(struct wzs *S)
{
    S->bitbuf = 0; S->bitcnt = 0;   /* descarta bits restantes del byte: en realidad hay que devolver bytes completos */
    return 0;
}
static int fixed_tables(struct wzh *l, struct wzh *d)
{
    uint8_t len[288]; int i;
    for (i = 0; i < 144; i++) len[i] = 8;
    for (; i < 256; i++) len[i] = 9;
    for (; i < 280; i++) len[i] = 7;
    for (; i < 288; i++) len[i] = 8;
    build(l, len, 288);
    for (i = 0; i < 30; i++) len[i] = 5;
    build(d, len, 30); return 0;
}
static int deflate_body(struct wzs *S)
{
    int last;
    do {
        last = (int)getbits(S, 1); int type = (int)getbits(S, 2); if (S->err) return -1;
        if (type == 0) {
            /* devolver los bytes completos que quedaron en el acumulador */
            int drop = S->bitcnt & 7; S->bitbuf >>= drop; S->bitcnt -= drop;
            uint32_t len = getbits(S, 16), nlen = getbits(S, 16); if (S->err || (len ^ 0xFFFFu) != nlen) return -1;
            while (len && S->bitcnt >= 8) { if (!wb_reserve(S->out, 1) || S->out->len >= S->max) return -2; S->out->p[S->out->len++] = (char)(S->bitbuf & 255); S->bitbuf >>= 8; S->bitcnt -= 8; len--; }
            if (len) {
                if (S->pos + len > S->n) return -1;
                if (S->out->len + len > S->max || !wb_reserve(S->out, len)) return -2;
                memcpy(S->out->p + S->out->len, S->in + S->pos, len); S->out->len += len; S->pos += len;
            }
        } else if (type == 1 || type == 2) {
            struct wzh *l = (struct wzh *)kmalloc(sizeof(*l) * 2); if (!l) return -2; struct wzh *d = l + 1;
            int rc;
            if (type == 1) fixed_tables(l, d);
            else {
                int nlen = (int)getbits(S, 5) + 257, ndist = (int)getbits(S, 5) + 1, ncode = (int)getbits(S, 4) + 4;
                static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                uint8_t lengths[320]; memset(lengths, 0, sizeof lengths);
                if (S->err || nlen > 286 || ndist > 30) { kfree(l); return -1; }
                for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)getbits(S, 3);
                for (int i = ncode; i < 19; i++) lengths[order[i]] = 0;
                if (build(l, lengths, 19) != 0) { kfree(l); return -1; }
                int idx = 0; uint8_t ll[320]; memset(ll, 0, sizeof ll);
                while (idx < nlen + ndist) {
                    int sym = decode(S, l); if (sym < 0) { kfree(l); return -1; }
                    if (sym < 16) ll[idx++] = (uint8_t)sym;
                    else {
                        int len = 0, rep;
                        if (sym == 16) { if (idx == 0) { kfree(l); return -1; } len = ll[idx - 1]; rep = 3 + (int)getbits(S, 2); }
                        else if (sym == 17) rep = 3 + (int)getbits(S, 3); else rep = 11 + (int)getbits(S, 7);
                        if (idx + rep > nlen + ndist) { kfree(l); return -1; }
                        while (rep--) ll[idx++] = (uint8_t)len;
                    }
                    if (S->err) { kfree(l); return -1; }
                }
                if (ll[256] == 0) { kfree(l); return -1; }
                build(l, ll, nlen); build(d, ll + nlen, ndist);
            }
            rc = codes(S, l, d); kfree(l);
            if (rc < 0) return rc;
        } else return -1;
    } while (!last);
    return 0;
}

int wz_inflate(const uint8_t *in, uint32_t n, int fmt, struct wb *out, uint32_t max_out)
{
    struct wzs S; memset(&S, 0, sizeof S); S.in = in; S.n = n; S.out = out; S.max = max_out ? max_out : 0x7fffffffu;
    (void)stored;
    if (fmt == 3) { if (n >= 2 && in[0] == 0x1f && in[1] == 0x8b) fmt = 2; else if (n >= 2 && (in[0] & 0x0f) == 8 && ((in[0] << 8) | in[1]) % 31 == 0) fmt = 1; else fmt = 0; }
    if (fmt == 1) { if (n < 2) return -1; S.pos = 2; if (in[1] & 0x20) S.pos += 4; }
    else if (fmt == 2) {
        if (n < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return -1;
        int flg = in[3]; S.pos = 10;
        if (flg & 4) { if (S.pos + 2 > n) return -1; uint32_t xl = in[S.pos] | (in[S.pos + 1] << 8); S.pos += 2 + xl; }
        if (flg & 8) { while (S.pos < n && in[S.pos]) S.pos++; S.pos++; }
        if (flg & 16) { while (S.pos < n && in[S.pos]) S.pos++; S.pos++; }
        if (flg & 2) S.pos += 2;
        if (S.pos >= n) return -1;
    }
    return deflate_body(&S);
}
