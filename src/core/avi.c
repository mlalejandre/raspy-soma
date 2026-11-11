/* SOMA_PARCHE_022_VIDEO: ver avi.h */
#include "avi.h"

#define FCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define F_RIFF FCC('R','I','F','F')
#define F_AVI  FCC('A','V','I',' ')
#define F_LIST FCC('L','I','S','T')
#define F_HDRL FCC('h','d','r','l')
#define F_AVIH FCC('a','v','i','h')
#define F_STRL FCC('s','t','r','l')
#define F_STRH FCC('s','t','r','h')
#define F_STRF FCC('s','t','r','f')
#define F_MOVI FCC('m','o','v','i')
#define F_VIDS FCC('v','i','d','s')
#define F_AUDS FCC('a','u','d','s')

const char *avi_strerror(int c)
{
    switch (c) {
    case AVI_OK: return "ok";
    case AVI_E_FORMAT: return "no es un AVI";
    case AVI_E_TRUNC: return "cabeceras AVI incompletas";
    case AVI_E_DATA: return "AVI corrupto";
    case AVI_E_UNSUPPORTED: return "AVI no soportado (hace falta video MJPG y, si hay audio, PCM de 16 bits)";
    default: return "error desconocido";
    }
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

struct pctx {
    struct avi_info *in;
    int stream;                     /* indice del strl actual */
    uint32_t type, handler;         /* de strh */
    uint32_t scale, rate_;
    int err;
};

static void parse_strl(const uint8_t *b, uint64_t off, uint64_t end, struct pctx *x)
{
    x->type = 0; x->handler = 0; x->scale = 0; x->rate_ = 0;
    int have_h = 0;
    while (off + 8 <= end) {
        uint32_t id = rd32(b + off), sz = rd32(b + off + 4);
        uint64_t d = off + 8;
        if (sz > end - d) { x->err = AVI_E_DATA; return; }
        if (id == F_STRH) {
            if (sz < 28) { x->err = AVI_E_DATA; return; }
            x->type = rd32(b + d); x->handler = rd32(b + d + 4);
            x->scale = rd32(b + d + 20); x->rate_ = rd32(b + d + 24);
            have_h = 1;
        } else if (id == F_STRF && have_h) {
            struct avi_info *in = x->in;
            if (x->type == F_VIDS) {
                if (sz < 40) { x->err = AVI_E_DATA; return; }
                uint32_t w = rd32(b + d + 4), h = rd32(b + d + 8), comp = rd32(b + d + 16);
                int32_t sh = (int32_t)h;
                if (sh < 0) sh = -sh;                                      /* alto negativo = de arriba abajo; MJPG no lo usa */
                int m = lower(comp & 255) == 'm' && lower((comp >> 8) & 255) == 'j' && lower((comp >> 16) & 255) == 'p' && lower((comp >> 24) & 255) == 'g';
                int m2 = lower(x->handler & 255) == 'm' && lower((x->handler >> 8) & 255) == 'j' && lower((x->handler >> 16) & 255) == 'p' && lower((x->handler >> 24) & 255) == 'g';
                if (in->video_stream < 0) {
                    if (!(m || m2)) { x->err = AVI_E_UNSUPPORTED; return; }
                    if (w == 0 || sh == 0 || w > 4096 || (uint32_t)sh > 4096) { x->err = AVI_E_DATA; return; }
                    in->video_stream = x->stream; in->width = w; in->height = (uint32_t)sh;
                    if (x->scale && x->rate_) {
                        uint64_t us = (uint64_t)x->scale * 1000000ULL / x->rate_;
                        if (us >= 1000 && us <= 1000000) in->usec_per_frame = (uint32_t)us;
                    }
                }
            } else if (x->type == F_AUDS) {
                if (sz < 16) { x->err = AVI_E_DATA; return; }
                uint16_t tag = rd16(b + d), ch = rd16(b + d + 2), bits = rd16(b + d + 14);
                uint32_t rate = rd32(b + d + 4);
                if (in->audio_stream < 0) {
                    if (tag != 1 || bits != 16 || (ch != 1 && ch != 2) || rate < 4000 || rate > 192000) { x->err = AVI_E_UNSUPPORTED; return; }
                    in->audio_stream = x->stream; in->has_audio = 1; in->channels = ch; in->bits = bits; in->rate = rate;
                }
            }
        }
        off = d + sz + (sz & 1);
    }
}

int avi_parse(const uint8_t *buf, size_t len, struct avi_info *info)
{
    struct avi_info in;
    uint8_t *z = (uint8_t *)&in; for (size_t i = 0; i < sizeof in; ++i) z[i] = 0;
    in.video_stream = in.audio_stream = -1;
    if (len < 12 || rd32(buf) != F_RIFF || rd32(buf + 8) != F_AVI) return AVI_E_FORMAT;
    uint64_t riff_end = 8 + (uint64_t)rd32(buf + 4);
    uint64_t off = 12;
    int got_hdrl = 0, got_movi = 0;
    struct pctx x; x.in = &in; x.stream = 0; x.err = 0;
    while (!got_movi) {
        if (off + 8 > len) return got_hdrl ? AVI_E_TRUNC : (len < off + 8 ? AVI_E_TRUNC : AVI_E_FORMAT);
        uint32_t id = rd32(buf + off), sz = rd32(buf + off + 4);
        uint64_t d = off + 8;
        if (id == F_LIST) {
            if (sz < 4 || d + 4 > len) return AVI_E_TRUNC;
            uint32_t lt = rd32(buf + d);
            if (lt == F_HDRL) {
                uint64_t hend = d + sz;
                if (hend > len) return AVI_E_TRUNC;
                uint64_t p = d + 4;
                while (p + 8 <= hend) {
                    uint32_t cid = rd32(buf + p), csz = rd32(buf + p + 4);
                    uint64_t cd = p + 8;
                    if (csz > hend - cd) return AVI_E_DATA;
                    if (cid == F_AVIH) {
                        if (csz < 40) return AVI_E_DATA;
                        in.usec_per_frame = rd32(buf + cd); in.total_frames = rd32(buf + cd + 16);
                    } else if (cid == F_LIST && csz >= 4 && rd32(buf + cd) == F_STRL) {
                        parse_strl(buf, cd + 4, cd + csz, &x);
                        if (x.err) return x.err;
                        x.stream++;
                    }
                    p = cd + csz + (csz & 1);
                }
                got_hdrl = 1;
            } else if (lt == F_MOVI) {
                if (!got_hdrl) return AVI_E_DATA;
                in.movi_start = (uint32_t)(d + 4);
                uint64_t e = d + sz; if (e > riff_end && riff_end > d) e = riff_end;
                in.movi_end = e > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)e;
                got_movi = 1;
                break;
            }
        }
        if (sz > 0xFFFFFFF0u) return AVI_E_DATA;
        off = d + sz + (sz & 1);
    }
    if (in.video_stream < 0) return AVI_E_UNSUPPORTED;
    if (in.usec_per_frame == 0) return AVI_E_DATA;
    if (in.usec_per_frame < 1000 || in.usec_per_frame > 1000000) return AVI_E_DATA;
    if (in.movi_end < in.movi_start) return AVI_E_DATA;
    if (info) *info = in;
    return AVI_OK;
}

void avi_chunk_parse(const uint8_t h[8], struct avi_chunk *c)
{
    c->fourcc = rd32(h); c->size = rd32(h + 4); c->kind = AVI_K_OTHER; c->stream = -1;
    if (c->fourcc == F_LIST) { c->kind = AVI_K_LIST; return; }
    int a = h[0], b = h[1], t0 = h[2], t1 = h[3];
    int hx = (a >= '0' && a <= '9') ? a - '0' : (lower(a) >= 'a' && lower(a) <= 'f') ? lower(a) - 'a' + 10 : -1;
    int lx = (b >= '0' && b <= '9') ? b - '0' : (lower(b) >= 'a' && lower(b) <= 'f') ? lower(b) - 'a' + 10 : -1;
    if (hx < 0 || lx < 0) return;
    int s = hx * 16 + lx;
    if (t0 == 'd' && (t1 == 'c' || t1 == 'b')) { c->kind = AVI_K_VIDEO; c->stream = s; }
    else if (t0 == 'w' && t1 == 'b') { c->kind = AVI_K_AUDIO; c->stream = s; }
}

uint64_t avi_chunk_next(uint64_t off, const struct avi_chunk *c)
{
    if (c->kind == AVI_K_LIST) return off + 12;
    return off + 8 + (uint64_t)c->size + (c->size & 1);
}

int avi_walk(const uint8_t *buf, size_t len, const struct avi_info *info, uint64_t *pos, struct avi_chunk *c, uint64_t *data_off)
{
    uint64_t end = info->movi_end < len ? info->movi_end : len;
    if (*pos + 8 > end) return 0;
    avi_chunk_parse(buf + *pos, c);
    uint64_t need = c->kind == AVI_K_LIST ? 12 : 8 + (uint64_t)c->size;
    if (*pos + need > end) return AVI_E_DATA;
    *data_off = *pos + 8;
    *pos = avi_chunk_next(*pos, c);
    return 1;
}
