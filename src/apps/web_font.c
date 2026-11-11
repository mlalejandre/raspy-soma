#include "web_font.h"
#include "web_util.h"
#include "fb.h"
#include "gui_font.h"
#include "web_fontdata.h"
#include "fonts/vraster.h"
#include "fonts/vfont.h"  /* copia propia (static) del rasterizador TrueType: no toca la fuente del escritorio */

static int wf_state = 0; /* 0 sin iniciar, 1 lista, -1 sin fuente */

int wf_init(void)
{
    if (wf_state == 0) wf_state = vfont_init(WEB_TTF_PTR, WEB_TTF_LEN) ? 1 : -1;
    return wf_state == 1;
}

static int clamp_px(int px) { return px < 5 ? 5 : (px > VF_MAX_EM ? VF_MAX_EM : px); }

static uint32_t glyph_of(uint32_t cp)
{
    uint32_t g = (cp < 256) ? vf_ascii[cp] : vf_lookup(cp);
    if (g == 0 && cp != '?') g = vf_ascii[(uint8_t)'?'];
    return g;
}

int wf_cp_adv64(uint32_t cp, int px, int fl)
{
    px = clamp_px(px);
    if (fl & WF_MONO) return ((px * 1233 + 1024) / 2048) * 64;
    if (cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || cp == 0xFEFF) return 0;
    if (cp == 0xA0) cp = ' ';
    if (!wf_init()) return px * 32;
    uint32_t g = glyph_of(cp);
    if (!g) return px * 32;
    int w = (int)(((uint64_t)vf_adv(g) * (uint64_t)px * 64u + vf.upem / 2) / vf.upem);
    if (fl & WF_BOLD) w += (px >= 20) ? 64 : 32;
    return w;
}

int wf_width64(const char *s, uint32_t n, int px, int fl)
{
    int w = 0; uint32_t i = 0;
    while (i < n && s[i]) { uint32_t adv; uint32_t cp = wu_utf8_next(s + i, &adv); i += adv; w += wf_cp_adv64(cp, px, fl); }
    return w;
}

int wf_width(const char *s, uint32_t n, int px, int fl) { return (wf_width64(s, n, px, fl) + 63) >> 6; }
int wf_ascent(int px) { return (px * 93 + 50) / 100; }
int wf_descent(int px) { return (px * 24 + 50) / 100; }

static void draw_glyph(int32_t x, int32_t by, struct vf_ce *ce, uint32_t rgb, int alpha, int fl, int px)
{
    if (!ce || !ce->cov) return;
    int32_t gx = x + ce->bx, gy = by + ce->by;
    if (!(fl & WF_ITALIC)) { fb_draw_mask8(gx, gy, ce->cov, ce->w, ce->h, rgb, (uint32_t)alpha); return; }
    int bands = ce->h >= 12 ? 4 : (ce->h >= 6 ? 2 : 1);
    int bh = (ce->h + bands - 1) / bands;
    (void)px;
    for (int r = 0; r < ce->h; r += bh) {
        int hh = (r + bh > ce->h) ? ce->h - r : bh;
        int cy = ce->by + r + hh / 2;                 /* y relativa a la linea base (negativa = arriba) */
        int dx = (-cy * 3) / 16;
        fb_draw_mask8(gx + dx, gy + r, ce->cov + r * ce->w, ce->w, hh, rgb, (uint32_t)alpha);
    }
}

void wf_draw(int32_t x, int32_t baseline_y, const char *s, uint32_t n, int px, uint32_t rgb, int fl, int alpha)
{
    if (!s || alpha <= 0) return;
    px = clamp_px(px);
    int64_t pen = (int64_t)x * 64; uint32_t i = 0;
    int mono_ok = (fl & WF_MONO) && font_vec_available();
    while (i < n && s[i]) {
        uint32_t adv; uint32_t cp = wu_utf8_next(s + i, &adv); i += adv;
        int a64 = wf_cp_adv64(cp, px, fl);
        int32_t gx = (int32_t)((pen + 32) >> 6);
        if (cp > 32 && cp != 0xA0 && a64) {
            if (mono_ok) {
                font_vec_draw(gx, baseline_y, cp, rgb | (uint32_t)(alpha & 255) << 24, px, a64 >> 6, 0);
            } else if (wf_init()) {
                uint32_t g = glyph_of(cp);
                if (g) {
                    struct vf_ce *ce = vf_get(g, px);
                    if (fl & WF_MONO) { int nat = (int)(((uint64_t)vf_adv(g) * (uint64_t)px + vf.upem / 2) / vf.upem); int off = ((a64 >> 6) - nat) / 2; gx += off; }
                    draw_glyph(gx, baseline_y, ce, rgb, alpha, fl, px);
                    if (fl & WF_BOLD) draw_glyph(gx + (px >= 20 ? 2 : 1), baseline_y, ce, rgb, alpha, fl, px);
                }
            }
        }
        pen += a64;
    }
}
