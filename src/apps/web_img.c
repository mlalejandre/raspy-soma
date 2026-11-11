/* SOMA_PARCHE_NAVEGADOR: decodificadores PNG / GIF / BMP (+ enlace con el JPEG del sistema). Solo enteros. */
#include "web_img.h"
#include "web_inflate.h"
#include "web_util.h"
#ifndef WEB_HOST_TEST
#include "image.h"
#endif
#include "jpeg.h"
#include "web_jpeg.h"

void wi_free(struct wimg *o) { if (o && o->px) kfree(o->px); if (o) { o->px = 0; o->w = o->h = 0; } }

/* ------------------------------ PNG ------------------------------ */
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c) { int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p; return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }

/* des-filtra una imagen (o pasada) de w x h: cada fila = 1 byte de filtro + stride bytes */
static int unfilter(uint8_t *d, int h, int stride, int bpp)
{
    uint8_t *prev = 0;
    for (int y = 0; y < h; y++) {
        uint8_t *row = d + (size_t)y * (stride + 1); int f = row[0]; row++;
        switch (f) {
        case 0: break;
        case 1: for (int i = bpp; i < stride; i++) row[i] = (uint8_t)(row[i] + row[i - bpp]); break;
        case 2: if (prev) for (int i = 0; i < stride; i++) row[i] = (uint8_t)(row[i] + prev[i]); break;
        case 3: for (int i = 0; i < stride; i++) { int a = i >= bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0; row[i] = (uint8_t)(row[i] + ((a + b) >> 1)); } break;
        case 4: for (int i = 0; i < stride; i++) { int a = i >= bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0, c = (i >= bpp && prev) ? prev[i - bpp] : 0; row[i] = (uint8_t)(row[i] + paeth(a, b, c)); } break;
        default: return -1;
        }
        prev = row;
    }
    return 0;
}

struct pngi { int w, h, depth, ctype, interlace; uint32_t pal[256]; int npal; int has_trns; uint16_t trns_g, trns_r, trns_gg, trns_b; uint8_t trns_a[256]; };

static uint32_t png_px(const struct pngi *I, const uint8_t *row, int x)
{
    int d = I->depth;
    switch (I->ctype) {
    case 0: {
        int v, maxv = (1 << d) - 1;
        if (d == 16) { v = (row[x * 2] << 8) | row[x * 2 + 1]; int g = row[x * 2]; uint32_t a = (I->has_trns && v == I->trns_g) ? 0 : 255; return (a << 24) | (g << 16) | (g << 8) | g; }
        int bit = x * d; v = (row[bit >> 3] >> (8 - d - (bit & 7))) & maxv; int g = v * 255 / maxv;
        uint32_t a = (I->has_trns && v == I->trns_g) ? 0 : 255; return (a << 24) | (g << 16) | (g << 8) | g; }
    case 2: {
        int r, g, b, rv, gv, bv;
        if (d == 16) { rv = (row[x * 6] << 8) | row[x * 6 + 1]; gv = (row[x * 6 + 2] << 8) | row[x * 6 + 3]; bv = (row[x * 6 + 4] << 8) | row[x * 6 + 5]; r = row[x * 6]; g = row[x * 6 + 2]; b = row[x * 6 + 4]; }
        else { r = rv = row[x * 3]; g = gv = row[x * 3 + 1]; b = bv = row[x * 3 + 2]; }
        uint32_t a = (I->has_trns && rv == I->trns_r && gv == I->trns_gg && bv == I->trns_b) ? 0 : 255;
        return (a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }
    case 3: {
        int bit = x * d; int v = (row[bit >> 3] >> (8 - d - (bit & 7))) & ((1 << d) - 1);
        if (v >= I->npal) return 0xFF000000u;
        uint32_t c = I->pal[v] & 0xFFFFFF; uint32_t a = I->has_trns ? I->trns_a[v] : 255; return (a << 24) | c; }
    case 4: {
        int g, a; if (d == 16) { g = row[x * 4]; a = row[x * 4 + 2]; } else { g = row[x * 2]; a = row[x * 2 + 1]; }
        return ((uint32_t)a << 24) | ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g; }
    default: {
        int r, g, b, a; if (d == 16) { r = row[x * 8]; g = row[x * 8 + 2]; b = row[x * 8 + 4]; a = row[x * 8 + 6]; } else { r = row[x * 4]; g = row[x * 4 + 1]; b = row[x * 4 + 2]; a = row[x * 4 + 3]; }
        return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }
    }
}

static int png_decode(const uint8_t *d, uint32_t n, struct wimg *o)
{
    struct pngi I; memset(&I, 0, sizeof I); struct wb idat = {0};
    uint32_t pos = 8; int got_hdr = 0, rc = -1;
    while (pos + 12 <= n) {
        uint32_t len = be32(d + pos); const uint8_t *t = d + pos + 4, *c = d + pos + 8;
        if (len > n || pos + 12 + len > n) break;
        if (!memcmp(t, "IHDR", 4) && len >= 13) {
            I.w = (int)be32(c); I.h = (int)be32(c + 4); I.depth = c[8]; I.ctype = c[9]; I.interlace = c[12]; got_hdr = 1;
            if (I.w <= 0 || I.h <= 0 || (uint64_t)I.w * (uint64_t)I.h > WI_MAX_PIXELS) goto out;
        } else if (!memcmp(t, "PLTE", 4)) { I.npal = (int)(len / 3); if (I.npal > 256) I.npal = 256; for (int i = 0; i < I.npal; i++) I.pal[i] = 0xFF000000u | (c[i * 3] << 16) | (c[i * 3 + 1] << 8) | c[i * 3 + 2]; }
        else if (!memcmp(t, "tRNS", 4)) {
            I.has_trns = 1;
            if (I.ctype == 3) { for (uint32_t i = 0; i < len && i < 256; i++) I.trns_a[i] = c[i]; for (uint32_t i = len; i < 256; i++) I.trns_a[i] = 255; }
            else if (I.ctype == 0 && len >= 2) I.trns_g = (uint16_t)((c[0] << 8) | c[1]);
            else if (I.ctype == 2 && len >= 6) { I.trns_r = (uint16_t)((c[0] << 8) | c[1]); I.trns_gg = (uint16_t)((c[2] << 8) | c[3]); I.trns_b = (uint16_t)((c[4] << 8) | c[5]); }
        } else if (!memcmp(t, "IDAT", 4)) { if (idat.len + len > (48u << 20)) goto out; wb_addn(&idat, (const char *)c, len); if (idat.err) goto out; }
        else if (!memcmp(t, "IEND", 4)) break;
        pos += 12 + len;
    }
    if (!got_hdr || !idat.len) goto out;
    int chans = I.ctype == 0 ? 1 : I.ctype == 2 ? 3 : I.ctype == 3 ? 1 : I.ctype == 4 ? 2 : 4;
    if (I.ctype != 0 && I.ctype != 2 && I.ctype != 3 && I.ctype != 4 && I.ctype != 6) goto out;
    if (I.depth != 1 && I.depth != 2 && I.depth != 4 && I.depth != 8 && I.depth != 16) goto out;
    int bits = chans * I.depth, bpp = (bits + 7) / 8;
    struct wb raw = {0};
    if (wz_inflate((const uint8_t *)idat.p, idat.len, 1, &raw, 160u << 20) < 0 && raw.len == 0) { wb_free(&raw); goto out; }
    o->px = (uint32_t *)kmalloc((size_t)I.w * I.h * 4); if (!o->px) { wb_free(&raw); goto out; }
    memset(o->px, 0, (size_t)I.w * I.h * 4); o->w = I.w; o->h = I.h;
    uint8_t *data = (uint8_t *)raw.p; size_t off = 0;
    static const int sx[7] = { 0, 4, 0, 2, 0, 1, 0 }, sy[7] = { 0, 0, 4, 0, 2, 0, 1 }, dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
    int passes = I.interlace ? 7 : 1; rc = 0;
    for (int p = 0; p < passes && rc == 0; p++) {
        int pw = I.interlace ? (I.w - sx[p] + dx[p] - 1) / dx[p] : I.w, ph = I.interlace ? (I.h - sy[p] + dy[p] - 1) / dy[p] : I.h;
        if (pw <= 0 || ph <= 0) continue;
        int stride = (pw * bits + 7) / 8; size_t need = (size_t)ph * (stride + 1);
        if (off + need > raw.len) { /* datos truncados: se rellena lo que haya */ int avail = (int)((raw.len - off) / (stride + 1)); if (avail <= 0) { rc = 0; break; } ph = avail; need = (size_t)ph * (stride + 1); rc = 0; }
        if (unfilter(data + off, ph, stride, bpp) < 0) { rc = -1; break; }
        for (int y = 0; y < ph; y++) {
            if ((y & 15) == 0) wu_tick();
            const uint8_t *row = data + off + (size_t)y * (stride + 1) + 1;
            int Y = I.interlace ? sy[p] + y * dy[p] : y;
            for (int x = 0; x < pw; x++) { int X = I.interlace ? sx[p] + x * dx[p] : x; o->px[(size_t)Y * I.w + X] = png_px(&I, row, x); }
        }
        off += need;
    }
    wb_free(&raw);
    if (rc < 0) { wi_free(o); }
out:
    wb_free(&idat);
    return rc;
}

/* ------------------------------ GIF ------------------------------ */
static int gif_decode(const uint8_t *d, uint32_t n, struct wimg *o)
{
    if (n < 13) return -1;
    int W = d[6] | (d[7] << 8), H = d[8] | (d[9] << 8), flags = d[10]; uint32_t pos = 13;
    if (W <= 0 || H <= 0 || (uint64_t)W * H > WI_MAX_PIXELS) return -1;
    uint32_t gpal[256]; int gn = 0;
    if (flags & 0x80) { gn = 2 << (flags & 7); if (pos + (uint32_t)gn * 3 > n) return -1; for (int i = 0; i < gn; i++) gpal[i] = 0xFF000000u | (d[pos + i * 3] << 16) | (d[pos + i * 3 + 1] << 8) | d[pos + i * 3 + 2]; pos += (uint32_t)gn * 3; }
    int trans = -1;
    while (pos < n) {
        int b = d[pos++];
        if (b == 0x21) {
            if (pos >= n) return -1;
            int lab = d[pos++];
            if (lab == 0xF9 && pos + 5 < n && d[pos] >= 4) { if (d[pos + 1] & 1) trans = d[pos + 4]; }
            while (pos < n && d[pos]) pos += d[pos] + 1u;
            pos++;
        } else if (b == 0x2C) {
            if (pos + 9 > n) return -1;
            int ix = d[pos] | (d[pos + 1] << 8), iy = d[pos + 2] | (d[pos + 3] << 8), iw = d[pos + 4] | (d[pos + 5] << 8), ih = d[pos + 6] | (d[pos + 7] << 8), lf = d[pos + 8]; pos += 9;
            uint32_t lpal[256]; uint32_t *pal = gpal; int pn = gn;
            if (lf & 0x80) { pn = 2 << (lf & 7); if (pos + (uint32_t)pn * 3 > n) return -1; for (int i = 0; i < pn; i++) lpal[i] = 0xFF000000u | (d[pos + i * 3] << 16) | (d[pos + i * 3 + 1] << 8) | d[pos + i * 3 + 2]; pal = lpal; pos += (uint32_t)pn * 3; }
            if (pos >= n || iw <= 0 || ih <= 0) return -1;
            int minc = d[pos++]; if (minc < 2 || minc > 11) return -1;
            struct wb sub = {0};
            while (pos < n && d[pos]) { uint32_t l = d[pos]; if (pos + 1 + l > n) l = n - pos - 1; wb_addn(&sub, (const char *)d + pos + 1, l); pos += d[pos] + 1u; }
            if (sub.err) { wb_free(&sub); return -1; }
            uint8_t *idx = (uint8_t *)kmalloc((size_t)iw * ih + 1); if (!idx) { wb_free(&sub); return -1; }
            memset(idx, 0, (size_t)iw * ih);
            /* LZW */
            uint16_t *prefix = (uint16_t *)kmalloc(4096 * 2 + 4096 + 4096); uint8_t *suffix = prefix ? (uint8_t *)(prefix + 4096) : 0; uint8_t *stack = suffix ? suffix + 4096 : 0;
            if (!prefix) { kfree(idx); wb_free(&sub); return -1; }
            int clear = 1 << minc, eoi = clear + 1, next = clear + 2, csz = minc + 1, prev = -1; uint32_t bitbuf = 0; int bitcnt = 0; size_t sp = 0, outp = 0, ip = 0;
            const uint8_t *src = (const uint8_t *)sub.p; size_t slen = sub.len; uint8_t first = 0; size_t total = (size_t)iw * ih;
            for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (uint8_t)i; }
            while (outp < total) {
                while (bitcnt < csz && ip < slen) { bitbuf |= (uint32_t)src[ip++] << bitcnt; bitcnt += 8; }
                if (bitcnt < csz) break;
                int code = (int)(bitbuf & ((1u << csz) - 1)); bitbuf >>= csz; bitcnt -= csz;
                if (code == clear) { next = clear + 2; csz = minc + 1; prev = -1; continue; }
                if (code == eoi) break;
                int cur = code;
                if (prev < 0) { if (code >= clear) break; idx[outp++] = (uint8_t)code; prev = code; first = (uint8_t)code; continue; }
                if (code >= next) { if (code > next) break; stack[sp++] = first; cur = prev; }
                while (cur >= clear && sp < 4095) { stack[sp++] = suffix[cur]; cur = prefix[cur]; if (cur == 0xFFFF) break; }
                if (cur != 0xFFFF) { first = (uint8_t)cur; stack[sp++] = first; }
                while (sp && outp < total) idx[outp++] = stack[--sp];
                sp = 0;
                if (next < 4096) { prefix[next] = (uint16_t)prev; suffix[next] = first; next++; if (next == (1 << csz) && csz < 12) csz++; }
                prev = code;
            }
            kfree(prefix); wb_free(&sub);
            o->px = (uint32_t *)kmalloc((size_t)W * H * 4); if (!o->px) { kfree(idx); return -1; }
            memset(o->px, 0, (size_t)W * H * 4); o->w = W; o->h = H;
            int inter = lf & 0x40; int row = 0;
            static const int istart[4] = { 0, 4, 2, 1 }, istep[4] = { 8, 8, 4, 2 };
            int pass = 0, y = inter ? 0 : 0;
            for (row = 0; row < ih; row++) {
                int Y = inter ? y : row;
                if (inter) { y += istep[pass]; while (y >= ih && pass < 3) { pass++; y = istart[pass]; } }
                int DY = iy + Y; if (DY < 0 || DY >= H) continue;
                for (int x = 0; x < iw; x++) { int DX = ix + x; if (DX < 0 || DX >= W) continue; int v = idx[(size_t)row * iw + x]; if (v == trans || v >= pn) continue; o->px[(size_t)DY * W + DX] = pal[v]; }
            }
            kfree(idx); return 0;
        } else if (b == 0x3B) break;
        else return -1;
    }
    return -1;
}

/* ------------------------------ JPEG / BMP ------------------------------ */
static int jpeg_to_img(const uint8_t *d, uint32_t n, struct wimg *o)
{
    struct jpeg_info inf;
    int r = jpeg_get_info(d, n, &inf);
    if (r == JPEG_E_UNSUPPORTED) return wj_decode_progressive(d, n, o);
    if (r != JPEG_OK) return -1;
    if (inf.width == 0 || inf.height == 0 || (uint64_t)inf.width * inf.height > WI_MAX_PIXELS) return -1;
    o->px = (uint32_t *)kmalloc((size_t)inf.width * inf.height * 4); if (!o->px) return -1;
    r = jpeg_decode(d, n, o->px, inf.width, inf.height, &inf);
    if (r != JPEG_OK && r != JPEG_E_TRUNC && r != JPEG_E_DATA) { kfree(o->px); o->px = 0; return -1; }
    o->w = (int)inf.width; o->h = (int)inf.height;
    for (size_t i = 0; i < (size_t)o->w * o->h; i++) o->px[i] |= 0xFF000000u;
    return 0;
}

int wi_decode(const uint8_t *d, uint32_t n, struct wimg *o)
{
    o->w = o->h = 0; o->px = 0;
    if (n < 8) return -1;
    if (!memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return png_decode(d, n, o);
    if (!memcmp(d, "GIF8", 4)) return gif_decode(d, n, o);
    if (d[0] == 0xFF && d[1] == 0xD8) return jpeg_to_img(d, n, o);
#ifndef WEB_HOST_TEST
    if (d[0] == 'B' && d[1] == 'M') {
        struct image im; memset(&im, 0, sizeof im);
        if (image_decode(d, n, &im) == 0 && im.pixels && im.width && im.height && (uint64_t)im.width * im.height <= WI_MAX_PIXELS) {
            o->px = (uint32_t *)kmalloc((size_t)im.width * im.height * 4);
            if (o->px) { for (size_t i = 0; i < (size_t)im.width * im.height; i++) o->px[i] = im.pixels[i] | 0xFF000000u; o->w = (int)im.width; o->h = (int)im.height; }
            image_free(&im); return o->px ? 0 : -1;
        }
        image_free(&im);
    }
#endif
    return -1;
}
