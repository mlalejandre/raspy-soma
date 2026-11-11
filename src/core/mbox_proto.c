#include "mbox_proto.h"

#define TAG_ALLOC   0x00040001u
#define TAG_PITCH   0x00040008u
#define TAG_SETPHYS 0x00048003u
#define TAG_SETVIRT 0x00048004u
#define TAG_SETDEPTH 0x00048005u
#define TAG_SETORDER 0x00048006u
#define TAG_SETOFFS 0x00048009u

static uint32_t put_tag(uint32_t *b, uint32_t i, uint32_t id, uint32_t n, uint32_t v0, uint32_t v1)
{
    b[i++] = id; b[i++] = n * 4; b[i++] = 0;
    b[i++] = v0; if (n > 1) b[i++] = v1;
    return i;
}

uint32_t mbox_fb_build(uint32_t *buf, uint32_t cap, uint32_t w, uint32_t h, uint32_t rgb)
{
    if (cap < MBOX_FB_WORDS || w < 1 || h < 1 || w > 8192 || h > 8192) return 0;
    uint32_t i = 2;
    buf[1] = 0;
    i = put_tag(buf, i, TAG_SETPHYS, 2, w, h);
    i = put_tag(buf, i, TAG_SETVIRT, 2, w, h);
    i = put_tag(buf, i, TAG_SETDEPTH, 1, 32, 0);
    i = put_tag(buf, i, TAG_SETORDER, 1, rgb ? 1u : 0u, 0);
    i = put_tag(buf, i, TAG_SETOFFS, 2, 0, 0);
    i = put_tag(buf, i, TAG_ALLOC, 2, 16, 0);              /* alineacion 16; la respuesta trae base y tamano */
    i = put_tag(buf, i, TAG_PITCH, 1, 0, 0);
    buf[i++] = 0;                                          /* etiqueta final */
    while (i < MBOX_FB_WORDS) buf[i++] = 0;
    buf[0] = i * 4;
    return i;
}

int mbox_fb_parse(const uint32_t *buf, uint32_t words, struct mbox_fb_result *r)
{
    uint32_t i = 2, seen = 0;
    if (words < 3 || buf[0] > words * 4 || buf[1] != 0x80000000u) return -1;
    uint32_t end = buf[0] / 4;
    *r = (struct mbox_fb_result){ 0 };
    while (i + 3 <= end && buf[i]) {
        uint32_t id = buf[i], sz = buf[i + 1], rc = buf[i + 2];
        if ((sz & 3) || sz > 64 || i + 3 + sz / 4 > end) return -2;
        if (!(rc & 0x80000000u)) return -2;                 /* el firmware no ha atendido la etiqueta */
        const uint32_t *v = &buf[i + 3];
        uint32_t n = sz / 4;
        if (id == TAG_ALLOC && n >= 2) { r->base = v[0]; r->size = v[1]; seen |= 1; }
        else if (id == TAG_PITCH && n >= 1) { r->pitch = v[0]; seen |= 2; }
        else if (id == TAG_SETPHYS && n >= 2) { r->width = v[0]; r->height = v[1]; seen |= 4; }
        else if (id == TAG_SETDEPTH && n >= 1) { r->depth = v[0]; seen |= 8; }
        i += 3 + n;
    }
    if (seen != 15) return -3;
    if (!r->base || r->depth != 32 || r->pitch < r->width * 4 || (uint64_t)r->pitch * r->height > r->size) return -4;
    return 0;
}

/* PARCHE 075 */
#define TAG_GETPHYS  0x00040003u
#define TAG_GETVIRT  0x00040004u
#define TAG_GETDEPTH 0x00040005u

uint32_t mbox_fb_build_query(uint32_t *buf, uint32_t cap)
{
    if (cap < MBOX_FB_WORDS) return 0;
    uint32_t i = 2;
    buf[1] = 0;
    i = put_tag(buf, i, TAG_GETPHYS, 2, 0, 0);
    i = put_tag(buf, i, TAG_GETDEPTH, 1, 0, 0);
    i = put_tag(buf, i, TAG_ALLOC, 2, 16, 0);
    i = put_tag(buf, i, TAG_PITCH, 1, 0, 0);
    buf[i++] = 0;
    while (i < MBOX_FB_WORDS) buf[i++] = 0;
    buf[0] = i * 4;
    return i;
}

int mbox_fb_parse_any(const uint32_t *buf, uint32_t words, struct mbox_fb_result *r)
{
    uint32_t i = 2, depth_seen = 0, pitch_seen = 0;
    if (words < 3 || buf[0] > words * 4 || buf[1] != 0x80000000u) return -1;
    uint32_t end = buf[0] / 4;
    *r = (struct mbox_fb_result){ 0 };
    while (i + 3 <= end && buf[i]) {
        uint32_t id = buf[i], sz = buf[i + 1], rc = buf[i + 2];
        if ((sz & 3) || sz > 64 || i + 3 + sz / 4 > end) return -2;
        const uint32_t *v = &buf[i + 3];
        uint32_t n = sz / 4;
        if (rc & 0x80000000u) {                                /* solo las etiquetas atendidas */
            if (id == TAG_ALLOC && n >= 2) { r->base = v[0]; r->size = v[1]; }
            else if (id == TAG_PITCH && n >= 1) { r->pitch = v[0]; pitch_seen = r->pitch != 0; }
            else if ((id == TAG_SETPHYS || id == TAG_GETPHYS) && n >= 2) { if (v[0] && v[1]) { r->width = v[0]; r->height = v[1]; } }
            else if ((id == TAG_SETVIRT || id == TAG_GETVIRT) && n >= 2) { if (!r->width && v[0] && v[1]) { r->width = v[0]; r->height = v[1]; } }
            else if ((id == TAG_SETDEPTH || id == TAG_GETDEPTH) && n >= 1) { r->depth = v[0]; depth_seen = 1; }
        }
        i += 3 + n;
    }
    if (!r->base || !r->width || !r->height || r->width > 8192 || r->height > 8192) return -4;
    /* PARCHE 077: el firmware de la Pi 5 declara 32 bits aunque la pantalla sea de 16 (RGB565) (raspberrypi/firmware#1904); lo fiable es la
     * pitch (bytes por fila) y, si falta, el tamano reservado. La profundidad declarada solo se usa si no hay ninguno de los dos. */
    {
        uint32_t k = 0;
        if (r->pitch && r->pitch % r->width == 0) k = r->pitch / r->width;
        if (k < 2 || k > 4) { k = 0; if (!r->pitch && r->size) k = (uint32_t)(r->size / ((uint64_t)r->width * r->height)); }
        if (k >= 2 && k <= 4) r->depth = k * 8;
        else if (!depth_seen || (r->depth != 16 && r->depth != 24 && r->depth != 32)) r->depth = 32;
        if (!r->pitch) r->pitch = r->width * (r->depth / 8);
        /* PARCHE 078: una pitch declarada por el firmware no se toca nunca (es el dato fiable); mbox_fb_fix resuelve las incoherencias */
        if (!pitch_seen && r->pitch < r->width * (r->depth / 8)) r->pitch = r->width * (r->depth / 8);
    }
    return 0;
}

/* PARCHE 078 */
int mbox_fb_consistent(const struct mbox_fb_result *r)
{
    if (!r->width || !r->height || !r->pitch || !r->size) return 0;
    if (r->pitch % r->width) return 0;
    uint32_t k = r->pitch / r->width;
    if (k < 2 || k > 4 || r->depth != k * 8) return 0;
    uint32_t rows = r->size / r->pitch;
    return rows >= r->height && rows - r->height <= r->height / 16 + 1;      /* el tamano puede venir redondeado hacia arriba */
}

int mbox_fb_fix(struct mbox_fb_result *r, uint32_t phys_w, uint32_t phys_h)
{
    if (mbox_fb_consistent(r)) return 0;
    if (!r->pitch || !r->size || r->size < r->pitch) return 0;               /* sin datos fiables: se deja como esta */
    uint32_t rows = r->size / r->pitch, k = 0;
    if (phys_w && r->pitch % phys_w == 0 && r->pitch / phys_w >= 2 && r->pitch / phys_w <= 4 && (!phys_h || phys_h <= rows)) k = r->pitch / phys_w;
    if (!k) {
        uint32_t w2 = r->pitch / 2;                                           /* RGB565: lo que usa el firmware de la Pi 5 */
        k = (r->pitch % 2 == 0 && w2 >= rows && w2 <= rows * 4) ? 2 : 4;       /* proporcion entre 1:1 y 4:1 */
        if (k == 4 && r->pitch % 4) k = 2;
    }
    r->width = r->pitch / k; r->height = (phys_h && phys_h <= rows && k == r->pitch / (phys_w ? phys_w : 1) ) ? phys_h : rows; r->depth = k * 8;
    return 1;
}

uint32_t mbox_fb_build_getphys(uint32_t *buf, uint32_t cap)
{
    if (cap < 8) return 0;
    uint32_t i = 2;
    buf[1] = 0;
    i = put_tag(buf, i, TAG_GETPHYS, 2, 0, 0);
    buf[i++] = 0;
    buf[0] = i * 4;
    return i;
}

int mbox_fb_parse_phys(const uint32_t *buf, uint32_t words, uint32_t *w, uint32_t *h)
{
    if (words < 8 || buf[1] != 0x80000000u || buf[2] != TAG_GETPHYS || !(buf[4] & 0x80000000u)) return -1;
    if (!buf[5] || !buf[6] || buf[5] > 8192 || buf[6] > 8192) return -1;
    *w = buf[5]; *h = buf[6];
    return 0;
}
