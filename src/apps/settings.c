/* PARCHE 085: Ajustes de la interfaz con la estetica del explorador. Apariencia: transparencia de las ventanas (40-100 %, con opcion
 * de dejar opaca la activa) y fuente de la interfaz (Sans o Mono, 90-120 %). Los cambios se aplican al momento y se guardan. */
#include "settings.h"
#include "fb.h"
#include "fs.h"
#include "web_font.h"
#include "gui_font.h"

#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_SIDE    0x000F1520
#define KC_LINE    0x00212B3B
#define KC_CARD    0x00111827
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_SELBG   0x001A3558
#define KC_PRIMARY 0x000B6BCB

static int g_opacity = 100, g_active_opaque = 0, g_loaded;
static int g_drag;
static char g_msg[96] = "Los cambios se aplican al momento y se guardan en /etc/ui.conf";
static const int SCALES[4] = { 90, 100, 110, 120 };
static const char *const SCALE_NAME[4] = { "Pequena", "Normal", "Grande", "Muy grande" };

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}

int settings_opacity(void) { return g_opacity; }
int settings_active_opaque(void) { return g_active_opaque; }

static void save(void)
{
    char b[128] = "opacidad="; s_num(b, sizeof b, g_opacity);
    s_cat(b, "\nactiva_opaca=", sizeof b); s_num(b, sizeof b, g_active_opaque);
    s_cat(b, "\nfuente=", sizeof b); s_cat(b, wf_ui_mono() ? "mono" : "sans", sizeof b);
    s_cat(b, "\nescala=", sizeof b); s_num(b, sizeof b, wf_ui_scale()); s_cat(b, "\n", sizeof b);
    (void)vfs_write("/etc/ui.conf", b, (uint32_t)s_len(b));
}
static int val_of(const char *t, const char *key, int def)
{
    int kl = s_len(key);
    for (int i = 0; t[i]; i++) {
        if ((i == 0 || t[i - 1] == '\n')) {
            int j = 0; while (j < kl && t[i + j] == key[j]) j++;
            if (j == kl && t[i + j] == '=') { const char *v = t + i + j + 1; if (v[0] == 'm') return 1; if (v[0] == 's') return 0; int n = 0; while (*v >= '0' && *v <= '9') n = n * 10 + (*v++ - '0'); return n; }
        }
    }
    return def;
}
void settings_load(void)
{
    if (g_loaded) return;
    g_loaded = 1;
    char b[160]; int n = vfs_read("/etc/ui.conf", b, sizeof b - 1);
    if (n <= 0) return;
    b[n] = 0;
    g_opacity = val_of(b, "opacidad", 100); if (g_opacity < 40) g_opacity = 40;
    if (g_opacity > 100) g_opacity = 100;
    g_active_opaque = val_of(b, "activa_opaca", 0) ? 1 : 0;
    wf_ui_set(val_of(b, "fuente", 0), val_of(b, "escala", 100));
}

/* ---------------------------------------------------------------- disposicion */
struct sl { int x, y, w, h, side_w, cx, cw, c1_y, c1_h, sl_x, sl_w, sl_y, tg_x, tg_y, prev_y, c2_y, c2_h, fam_y, sz_y, seg_w, demo_y, reset_x, reset_y; };
static void lay(struct sl *L, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    L->x = (int)x; L->y = (int)y; L->w = (int)w; L->h = (int)h;
    L->side_w = w > 640 ? 200 : 150;
    L->cx = L->x + L->side_w + 24; L->cw = L->x + L->w - 24 - L->cx;
    int cy = L->y + 84;
    L->c1_y = cy; L->c1_h = 190;
    L->sl_x = L->cx + 20; L->sl_w = L->cw - 40 - 70; L->sl_y = cy + 62;
    L->tg_x = L->cx + L->cw - 20 - 44; L->tg_y = cy + 100;
    L->prev_y = cy + 136;
    L->c2_y = cy + L->c1_h + 16; L->c2_h = 200;
    L->fam_y = L->c2_y + 52; L->sz_y = L->c2_y + 100; L->seg_w = L->cw - 40;
    L->demo_y = L->c2_y + 146;
    L->reset_x = L->cx + L->cw - 130; L->reset_y = L->c2_y + L->c2_h + 16;
}
static void card(int x, int y, int w, int h) { fb_fill_rrect_aa(x, y, w, h, 12, 0xFF000000u | KC_CARD); fb_stroke_rrect_aa(x, y, w, h, 12, 0xFF1E293Bu); }
static void seg(int x, int y, int w, int n, int sel, const char *const *lab)
{
    fb_fill_rounded_rect(x, y, w, 32, 10, 0x000C1119);
    int bw = (w - 4) / n;
    for (int i = 0; i < n; i++) {
        if (i == sel) fb_fill_rounded_rect(x + 2 + i * bw, y + 2, bw, 28, 8, 0x00283449);
        tx(x + 2 + i * bw + (bw - tw(lab[i], 12, i == sel)) / 2, y + 2, 28, lab[i], 12, i == sel ? 0x00FFFFFF : KC_DIM, i == sel);
    }
}

void settings_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focus)
{
    (void)focus;
    settings_load();
    struct sl L; lay(&L, x, y, w, h);
    char b[96];
    fb_fill_rect(x, y, w, h, KC_WIN);
    /* barra lateral */
    fb_fill_rect(x, y, (uint32_t)L.side_w, h, KC_SIDE);
    fb_fill_rect(x + (uint32_t)L.side_w - 1, y, 1, h, KC_LINE);
    tx(L.x + 18, L.y + 14, 30, "Ajustes", 18, KC_TXT, 1);
    static const char *const items[3] = { "Apariencia", "Pantalla", "Sistema" };
    for (int i = 0; i < 3; i++) {
        int iy = L.y + 60 + i * 40;
        if (i == 0) { fb_fill_rounded_rect(L.x + 10, iy, L.side_w - 20, 34, 9, KC_SELBG); fb_fill_rounded_rect(L.x + 10, iy + 8, 3, 18, 1, KC_ACC); }
        /* iconos: paleta, monitor, engranaje */
        int ix = L.x + 30, icy = iy + 17; uint32_t ic = i == 0 ? 0xFFFFFFFFu : 0xFF566379u;
        if (i == 0) { fb_stroke_rrect_aa(ix - 7, icy - 7, 14, 14, 7, ic); fb_fill_rrect_aa(ix - 3, icy - 3, 4, 4, 2, ic); }
        else if (i == 1) { fb_stroke_rrect_aa(ix - 8, icy - 6, 16, 11, 2, ic); fb_fill_rect((uint32_t)(ix - 3), (uint32_t)(icy + 6), 6, 2, ic & 0xFFFFFF); }
        else { fb_stroke_rrect_aa(ix - 6, icy - 6, 12, 12, 6, ic); fb_fill_rrect_aa(ix - 2, icy - 2, 4, 4, 2, ic); }
        tx(L.x + 46, iy, 34, items[i], 13, i == 0 ? 0x00FFFFFF : KC_FAINT, i == 0);
        if (i) tx(L.x + 46 + tw(items[i], 13, 0) + 8, iy, 34, "pronto", 10, 0x00475569, 0);
    }
    /* cabecera */
    tx(L.cx, L.y + 18, 30, "Apariencia", 20, KC_TXT, 1);
    tx(L.cx, L.y + 46, 22, "Transparencia de las ventanas y letra de las aplicaciones", 12, KC_DIM, 0);

    /* tarjeta 1: transparencia */
    card(L.cx, L.c1_y, L.cw, L.c1_h);
    tx(L.cx + 20, L.c1_y + 14, 22, "Transparencia de las ventanas", 14, KC_TXT, 1);
    tx(L.cx + 20, L.c1_y + 34, 18, "100 % = opacas. Se ve el escritorio a traves de las ventanas.", 11, KC_DIM, 0);
    {
        int ty = L.sl_y + 10, kx = L.sl_x + (g_opacity - 40) * (L.sl_w - 1) / 60;
        fb_fill_rounded_rect(L.sl_x, ty - 3, L.sl_w, 6, 3, 0x00263142);
        fb_fill_rounded_rect(L.sl_x, ty - 3, kx - L.sl_x + 1, 6, 3, KC_PRIMARY);
        fb_fill_rrect_aa(kx - 10, ty - 10, 20, 20, 10, 0xFFFFFFFFu);
        fb_stroke_rrect_aa(kx - 10, ty - 10, 20, 20, 10, 0x40000000u);
        b[0] = 0; s_num(b, sizeof b, g_opacity); s_cat(b, " %", sizeof b);
        tx(L.sl_x + L.sl_w + 18, L.sl_y, 20, b, 15, KC_TXT, 1);
    }
    tx(L.cx + 20, L.tg_y, 24, "Mantener opaca la ventana activa", 13, KC_TXT, 0);
    {
        int on = g_active_opaque;
        fb_fill_rrect_aa(L.tg_x, L.tg_y, 44, 24, 12, on ? (0xFF000000u | KC_PRIMARY) : 0xFF334155u);
        fb_fill_rrect_aa(L.tg_x + (on ? 22 : 2), L.tg_y + 2, 20, 20, 10, 0xFFFFFFFFu);
    }
    /* vista previa: "escritorio" con una ventana translucida encima */
    {
        int px = L.cx + 20, pw = L.cw - 40, ph = 38, py = L.prev_y;
        for (int i = 0; i < pw; i += 2) { uint32_t c = 0x001E3A8Au + (uint32_t)(i * 60 / pw) * 0x00010100u; fb_fill_rect((uint32_t)(px + i), (uint32_t)py, 2, (uint32_t)ph, c); }
        for (int k = 0; k < 6; k++) fb_fill_rrect_aa(px + 12 + k * (pw / 6), py + 8, 22, 22, 11, 0xFF000000u | (0x00F59E0Bu + (uint32_t)k * 0x00101030u));
        uint32_t a = (uint32_t)(g_opacity * 255 / 100);
        fb_fill_rect_alpha(px + pw / 4, py + 4, pw / 2, ph - 8, (a << 24) | KC_WIN);
        tx(px + pw / 4 + 12, py + 4, ph - 8, "Ventana de ejemplo", 12, KC_TXT, 0);
    }

    /* tarjeta 2: fuente */
    card(L.cx, L.c2_y, L.cw, L.c2_h);
    tx(L.cx + 20, L.c2_y + 14, 22, "Fuente de la interfaz", 14, KC_TXT, 1);
    tx(L.cx + 20, L.c2_y + 34, 18, "Afecta al texto de las aplicaciones (no al contenido de las paginas web).", 11, KC_DIM, 0);
    static const char *const fam[2] = { "DejaVu Sans", "DejaVu Sans Mono" };
    seg(L.cx + 20, L.fam_y, L.seg_w, 2, wf_ui_mono(), fam);
    int si = 1; for (int i = 0; i < 4; i++) if (SCALES[i] == wf_ui_scale()) si = i;
    seg(L.cx + 20, L.sz_y, L.seg_w, 4, si, SCALE_NAME);
    fb_fill_rounded_rect(L.cx + 20, L.demo_y, L.seg_w, 40, 10, 0x00090D15);
    fb_set_clip(L.cx + 20, L.demo_y, L.seg_w, 40);
    tx(L.cx + 34, L.demo_y, 40, "El veloz murcielago hindu comia kiwi. 0123", 14, KC_TXT, 0);
    fb_reset_clip();

    /* pie */
    fb_fill_rounded_rect(L.reset_x, L.reset_y, 130, 32, 16, 0x00182133);
    fb_stroke_rrect_aa(L.reset_x, L.reset_y, 130, 32, 16, 0xFF263142u);
    tx(L.reset_x + (130 - tw("Restablecer", 13, 1)) / 2, L.reset_y, 32, "Restablecer", 13, KC_TXT, 1);
    fb_set_clip(L.cx, L.reset_y, L.reset_x - L.cx - 10, 32);
    tx(L.cx, L.reset_y, 32, g_msg, 11, KC_FAINT, 0);
    fb_reset_clip();
}

static int set_opacity_from(const struct sl *L, int mx)
{
    int x = mx - L->sl_x; if (x < 0) x = 0;
    if (x > L->sl_w - 1) x = L->sl_w - 1;
    int v = 40 + x * 60 / (L->sl_w - 1);
    v = (v + 2) / 5 * 5;                                   /* de 5 en 5 */
    if (v == g_opacity) return 0;
    g_opacity = v; return 1;
}
int settings_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct sl L; lay(&L, x, y, w, h);
    g_drag = 0;
    if (my >= L.sl_y - 6 && my < L.sl_y + 26 && mx >= L.sl_x - 12 && mx < L.sl_x + L.sl_w + 12) { g_drag = 1; int c = set_opacity_from(&L, mx); if (c) save(); return c; }
    if (mx >= L.tg_x && mx < L.tg_x + 44 && my >= L.tg_y && my < L.tg_y + 24) { g_active_opaque = !g_active_opaque; save(); return 1; }
    if (my >= L.fam_y && my < L.fam_y + 32 && mx >= L.cx + 20 && mx < L.cx + 20 + L.seg_w) {
        int i = (mx - L.cx - 22) * 2 / (L.seg_w - 4); if (i > 1) i = 1;
        wf_ui_set(i, wf_ui_scale()); save(); return 1;
    }
    if (my >= L.sz_y && my < L.sz_y + 32 && mx >= L.cx + 20 && mx < L.cx + 20 + L.seg_w) {
        int i = (mx - L.cx - 22) * 4 / (L.seg_w - 4); if (i > 3) i = 3;
        if (i < 0) i = 0;
        wf_ui_set(wf_ui_mono(), SCALES[i]); save(); return 1;
    }
    if (mx >= L.reset_x && mx < L.reset_x + 130 && my >= L.reset_y && my < L.reset_y + 32) {
        g_opacity = 100; g_active_opaque = 0; wf_ui_set(0, 100); save();
        g_msg[0] = 0; s_cat(g_msg, "Valores por defecto restablecidos", sizeof g_msg);
        return 1;
    }
    return 0;
}
int settings_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int held)
{
    (void)my;
    if (!g_drag) return 0;
    if (!held) { g_drag = 0; save(); return 1; }
    struct sl L; lay(&L, x, y, w, h);
    return set_opacity_from(&L, mx);
}
int settings_handle_key(char c)
{
    if (c == '+' || c == '=') { if (g_opacity < 100) { g_opacity += 5; save(); return 1; } }
    if (c == '-') { if (g_opacity > 40) { g_opacity -= 5; save(); return 1; } }
    return 0;
}
