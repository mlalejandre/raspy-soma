/* SOMA_PARCHE_NAVEGADOR: navegador web profesional de SOMA.
 * Hilo GUI: dibuja y encola eventos. Hilo trabajador ("web_worker", pila propia de 1 MiB): red, HTML, CSS, JavaScript, maquetacion y temporizadores. */
#include "browser.h"
#include "web_page.h"
#include "web_font.h"
#include "fb.h"
#include "mouse.h"
#include "thread.h"
#include "idt.h"
#include "mem.h"
#include "fs.h"
#include "image.h"
#include "gui_font.h"
#include "web_dom.h"
#include "web_util.h"
#include "console.h"

#define MAX_TABS 6
#define HOME_URL "file:///workspace/demo_web.html"
#define WK_STACK (1024u * 1024u)
#define JQ 48

/* ------------------------------------------------------------ servicios que exige el motor web (tiempo, cesion, pila) */
uint64_t web_now_ms(void) { return timer_get_uptime_ms(); }
static struct tcb *s_wk_tcb;          /* solo el hilo trabajador cede desde los bucles largos del motor: la GUI nunca debe ceder a mitad de pintar */
void web_yield(void) { if (s_wk_tcb && thread_current() == s_wk_tcb) thread_yield(); }
void jpeg_yield_hook(void) { wu_tick(); }      /* SOMA_PARCHE_028_UI: el JPEG del sistema tambien cede desde el hilo del navegador */
static char *s_wk_lo, *s_wk_hi;       /* pila del hilo trabajador */
static char *s_gu_lo, *s_gu_hi;       /* pila grande del hilo de la GUI (dibujo y pruebas de impacto recursivos) */
uint32_t web_stack_left(void)
{
    char c; char *sp = &c;
    if (s_wk_lo && sp >= s_wk_lo && sp < s_wk_hi) return (uint32_t)(sp - s_wk_lo);
    if (s_gu_lo && sp >= s_gu_lo && sp < s_gu_hi) return (uint32_t)(sp - s_gu_lo);
    return 8000;                       /* pila desconocida (p. ej. la del nucleo, 64 KiB): ser conservador */
}
void web_call_on_stack(char *top, void (*fn)(void *), void *arg);
/* El arbol de cajas se recorre de forma recursiva: en el hilo de la GUI (pila corta) se hace sobre una pila propia de 512 KiB. */
#define GU_STACK (512u * 1024u)
static void on_gui_stack(void (*fn)(void *), void *arg)
{
    char c; char *sp = &c;
    if (s_gu_lo && sp >= s_gu_lo && sp < s_gu_hi) { fn(arg); return; }
    if (!s_gu_lo) {
        char *stk = (char *)kmalloc(GU_STACK);
        if (!stk) { fn(arg); return; }
        s_gu_hi = stk + GU_STACK; s_gu_lo = stk + 8192;
    }
    web_call_on_stack((char *)(((uintptr_t)s_gu_hi) & ~(uintptr_t)15), fn, arg);
}

/* ------------------------------------------------------------ estado */
struct tab { char url[512]; char title[64]; int scroll; struct wp_hist hist[WP_HIST]; int hn, hpos; };
enum { J_LOAD = 1, J_BACK, J_FWD, J_RELOAD, J_CLICK, J_KEY, J_SELECT, J_SWITCH, J_NEWTAB, J_CLOSE, J_EVAL, J_FIND, J_HOME };
struct job { int type, a, b, c; char *s; };

static struct wpage *s_pg;
static struct tab *s_tabs[MAX_TABS];
static int s_ntabs, s_cur;
static struct job s_q[JQ];
static volatile uint32_t s_qh, s_qt;
static volatile int s_job_busy, s_inited, s_worker_up;
static volatile uint64_t s_last_render;
static volatile int s_move_pending, s_move_x, s_move_y;
static uint32_t s_win_x, s_win_y, s_win_w, s_win_h;

static char s_url[1024]; static int s_url_len, s_url_cur, s_url_focus, s_url_all;
static char s_find[80]; static int s_find_len, s_find_open;
static char s_hover_href[300]; static char s_jserr[140]; static uint64_t s_jserr_t;
static int s_dd_hl = -1; static int s_sb_drag, s_sb_grab;
static uint8_t s_u8[4]; static int s_u8n, s_u8need;
static uint64_t s_last_click_t; static int s_last_cx, s_last_cy;
static int s_cursor_cur = -1;

/* ------------------------------------------------------------ servicios para web_page.c */
int wp_fs_read(const char *path, char **out, uint32_t *len)
{
    const uint32_t MAXF = 1u << 20;
    char *tmp = (char *)kmalloc(MAXF + 1); if (!tmp) return -1;
    int r = vfs_read(path, tmp, MAXF);
    if (r <= 0) {
        uint8_t *bm = (uint8_t *)tmp; int n = 0;
        if (!wu_strieq(path, "/workspace/logo_soma.bmp") && !wu_strieq(path, "/workspace/chip_soma.bmp")) { kfree(tmp); return -1; }
        if (wu_strieq(path, "/workspace/logo_soma.bmp")) n = image_create_demo_bmp(bm, 200000, 80, 40); else n = image_create_chip_bmp(bm, 200000, 64, 32);
        if (n <= 0) { kfree(tmp); return -1; }
        vfs_write(path, tmp, (uint32_t)n); r = n;
    }
    char *o = (char *)kmalloc((uint32_t)r + 1); if (!o) { kfree(tmp); return -1; }
    memcpy(o, tmp, (uint32_t)r); o[r] = 0; kfree(tmp); *out = o; *len = (uint32_t)r; return 0;
}
void wp_log(int level, const char *msg)
{
    if (level >= 2) { wu_copy(s_jserr, msg, sizeof s_jserr); s_jserr_t = web_now_ms(); }
    if (msg && (msg[0] == 'i' && msg[1] == 'm' && msg[2] == 'g')) { kprint("[web] "); kprint(msg); kprint("\n"); }   /* diagnostico de imagenes por el puerto serie */
}

/* ------------------------------------------------------------ cola de trabajos */
static void start_worker(void);
static int q_push(int type, int a, int b, int c, const char *s)
{
    start_worker();
    uint32_t nt = (s_qt + 1) % JQ; if (nt == s_qh) return 0;
    struct job *j = &s_q[s_qt]; j->type = type; j->a = a; j->b = b; j->c = c; j->s = 0;
    if (s) { uint32_t n = wu_strlen(s); j->s = (char *)kmalloc(n + 1); if (!j->s) return 0; memcpy(j->s, s, n + 1); }
    s_qt = nt; return 1;
}
static int q_pop(struct job *out)
{
    if (s_qh == s_qt) return 0;
    *out = s_q[s_qh]; s_qh = (s_qh + 1) % JQ; return 1;
}

/* ------------------------------------------------------------ pestanas (la pagina viva es una sola; las demas se descartan y se recargan al volver) */
static void tab_save(void)
{
    struct tab *t = s_tabs[s_cur]; if (!t || !s_pg) return;
    memcpy(t->hist, s_pg->hist, sizeof t->hist); t->hn = s_pg->hn; t->hpos = s_pg->hpos;
    wu_copy(t->url, s_pg->url, sizeof t->url); wu_copy(t->title, s_pg->title, sizeof t->title); t->scroll = s_pg->sy;
    if (t->hpos >= 0 && t->hpos < t->hn) t->hist[t->hpos].scroll_y = s_pg->sy;
}
static struct tab *tab_new(void)
{
    struct tab *t = (struct tab *)kmalloc(sizeof *t); if (!t) return 0; memset(t, 0, sizeof *t); t->hpos = -1; return t;
}
static void tab_load(int i)
{
    struct tab *t = s_tabs[i]; if (!t || !s_pg) return;
    s_cur = i;
    memcpy(s_pg->hist, t->hist, sizeof t->hist); s_pg->hn = t->hn; s_pg->hpos = t->hpos;
    if (t->url[0] && t->hpos >= 0) { s_pg->hist[t->hpos].scroll_y = t->scroll; wp_navigate(s_pg, t->url, WPN_NOHIST); }
    else wp_navigate(s_pg, HOME_URL, 0);
}

static void run_job(struct job *j)
{
    struct wpage *p = s_pg; if (!p) return;
    switch (j->type) {
    case J_LOAD: s_job_busy = 1; wp_navigate(p, j->s ? j->s : "", j->a ? WPN_REPLACE : 0); s_job_busy = 0; break;
    case J_HOME: s_job_busy = 1; wp_navigate(p, HOME_URL, 0); s_job_busy = 0; break;
    case J_BACK: s_job_busy = 1; wp_history_go(p, -1); s_job_busy = 0; break;
    case J_FWD: s_job_busy = 1; wp_history_go(p, 1); s_job_busy = 0; break;
    case J_RELOAD: s_job_busy = 1; wp_navigate(p, p->url, WPN_RELOAD); s_job_busy = 0; break;
    case J_CLICK: wp_mouse_click(p, j->a, j->b, j->c & 15, (j->c >> 4) & 15, (j->c >> 8) & 1); break;
    case J_KEY: wp_key(p, j->a, j->s ? j->s : "", j->c); break;
    case J_SELECT: if (p->dd_sel) wp_select_choose(p, p->dd_sel, j->a); break;
    case J_EVAL: { char o[64]; wp_eval_js(p, j->s ? j->s : "", o, sizeof o); break; }
    case J_FIND: wp_find_scroll(p, j->s ? j->s : ""); break;
    case J_SWITCH:
        if (j->a >= 0 && j->a < s_ntabs && j->a != s_cur) { s_job_busy = 1; tab_save(); tab_load(j->a); s_job_busy = 0; }
        break;
    case J_NEWTAB:
        if (s_ntabs < MAX_TABS) {
            struct tab *t = tab_new(); if (!t) break;
            s_job_busy = 1; tab_save(); s_tabs[s_ntabs] = t; s_cur = s_ntabs++;
            s_pg->hn = 0; s_pg->hpos = -1; wp_navigate(s_pg, j->s && j->s[0] ? j->s : HOME_URL, 0); s_job_busy = 0;
        }
        break;
    case J_CLOSE: {
        int i = j->a; if (i < 0 || i >= s_ntabs) break;
        if (s_ntabs == 1) { s_job_busy = 1; s_pg->hn = 0; s_pg->hpos = -1; wp_navigate(s_pg, "about:blank", 0); s_job_busy = 0; break; }
        if (i != s_cur) { kfree(s_tabs[i]); for (int k = i; k < s_ntabs - 1; k++) s_tabs[k] = s_tabs[k + 1]; s_ntabs--; if (s_cur > i) s_cur--; break; }
        kfree(s_tabs[i]); for (int k = i; k < s_ntabs - 1; k++) s_tabs[k] = s_tabs[k + 1]; s_ntabs--;
        int n = i < s_ntabs ? i : s_ntabs - 1; s_job_busy = 1; tab_load(n); s_job_busy = 0; break; }
    default: break;
    }
}
static void worker_loop(void *arg)
{
    (void)arg;
    for (;;) {
        struct job j; int worked = 0;
        while (q_pop(&j)) { run_job(&j); if (j.s) kfree(j.s); worked = 1; }
        uint64_t now = web_now_ms(); int next = 250;      /* ventana no visible: casi dormido (el SO en reposo no gasta CPU) */
        if (s_pg && now - s_last_render < 4000) {
            if (s_move_pending) { s_move_pending = 0; if (s_pg->lay) wp_mouse_move(s_pg, s_move_x + s_pg->sx, s_move_y + s_pg->sy); }
            next = wp_tick(s_pg, now);
            if (next < 2) next = 2;
            if (next > 40) next = 40;
        }
        if (!worked) thread_sleep((uint32_t)next); else thread_yield();
    }
}
static void worker_entry(void *arg)
{
    s_wk_tcb = thread_current();
    char *stk = (char *)kmalloc(WK_STACK);
    if (!stk) { worker_loop(arg); return; }
    s_wk_lo = stk + 4096; s_wk_hi = stk + WK_STACK;
    web_call_on_stack((char *)(((uintptr_t)stk + WK_STACK) & ~(uintptr_t)15), worker_loop, arg);
}
static int s_need_home;
static void start_worker(void)
{
    if (s_worker_up || !s_inited) return;
    s_last_render = web_now_ms();
    if (thread_create("web_worker", worker_entry, 0)) s_worker_up = 1;
}
/* Al arrancar la GUI solo se reserva el estado minimo: ni hilo, ni fuente, ni pagina hasta que se abre la ventana. */
static void ensure_init(void)
{
    if (s_inited) return;
    s_pg = wp_new();
    s_tabs[0] = tab_new(); s_ntabs = 1; s_cur = 0;
    if (!s_pg || !s_tabs[0]) return;
    s_inited = 1; s_need_home = 1;
    wu_copy(s_url, HOME_URL, sizeof s_url); s_url_len = (int)wu_strlen(s_url); s_url_cur = s_url_len;
}

/* ------------------------------------------------------------ dibujo */
/* SOMA_PARCHE_028_UI: interfaz renovada. La ventana ya tiene barra de titulo y marco propios del gestor de ventanas,
 * asi que aqui solo quedan la tira de pestanas, la barra de navegacion, la pagina y una linea de estado. */
#define TB_H 0
#define TAB_H 34
#define NAV_H 42
#define STAT_H 22
#define SB_W 12
#define NAVB 32            /* lado de los botones de navegacion */
#define NAVSTEP 36
#define C_BG 0x000F131C
#define C_PANEL 0x001A2030
#define C_LINE 0x002A3446
#define C_TXT 0x00F1F5F9
#define C_DIM 0x008A97AD
#define C_AMB 0x00F59E0B
#define C_GRN 0x0010B981
#define C_RED 0x00EF4444
#define C_CYA 0x0038BDF8
#define C_FIELD 0x000B0E14
#define C_HOVER 0x002A3349

static int s_font;
static int s_hover_ui = -1;        /* elemento de la interfaz bajo el puntero: 0..3 botones, 4 ir, 10+i pestana, 30+i cierre de pestana, 50 nueva pestana */
struct geo { int vx, vy, vw, vh, tab_y, nav_y, stat_y, ub_x, ub_w; };

static void geo_of(struct geo *g, int wx, int wy, int ww, int wh)
{
    g->tab_y = wy + TB_H; g->nav_y = g->tab_y + TAB_H; g->vx = wx; g->vy = g->nav_y + NAV_H + 2;
    g->vw = ww; g->stat_y = wy + wh - STAT_H; g->vh = g->stat_y - g->vy; if (g->vh < 32) g->vh = 32;
    g->ub_x = wx + 10 + 4 * NAVSTEP + 2; g->ub_w = (wx + ww - 10) - g->ub_x; if (g->ub_w < 60) g->ub_w = 60;
}
static int tab_width(int ww) { int t = (ww - 20 - 34) / (s_ntabs ? s_ntabs : 1); return t > 210 ? 210 : (t < 40 ? 40 : t); }
static int in_r(int px, int py, int x, int y, int w, int h) { return px >= x && py >= y && px < x + w && py < y + h; }
static int tx_w(const char *s, int n, int px, int fl)
{
    if (s_font) return wf_width(s, (uint32_t)n, px, fl);
    return n * 8;
}
static void tx(int x, int y, const char *s, int n, int px, uint32_t col, int fl)
{
    if (n <= 0) return;
    if (s_font) { wf_draw(x, y + wf_ascent(px), s, (uint32_t)n, px, col, fl, 255); return; }
    char b[160]; int k = n < 159 ? n : 159; memcpy(b, s, (uint32_t)k); b[k] = 0; font_draw_string((uint32_t)x, (uint32_t)y, b, col, 1);
}
/* recorta texto a un ancho maximo (por bytes UTF-8 completos) y devuelve los bytes a dibujar */
static int fit(const char *s, int maxw, int px, int fl)
{
    int n = (int)wu_strlen(s);
    if (tx_w(s, n, px, fl) <= maxw) return n;
    while (n > 0) { n--; while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--; if (tx_w(s, n, px, fl) <= maxw) break; }
    return n;
}
static void button(int x, int y, int w, int h, const char *lab, uint32_t edge, int on)
{
    fb_fill_rounded_rect(x, y, w, h, 7, on ? 0x000B6BCB : C_PANEL);
    int n = (int)wu_strlen(lab), lw = tx_w(lab, n, 13, 0);
    tx(x + (w - lw) / 2, y + (h - 16) / 2, lab, n, 13, on ? 0x00FFFFFF : 0x00475569, 0);
    (void)edge;
}
/* ---- iconos de la barra (trazos suavizados; coordenadas en 1/16 px) ---- */
#define LN(x0, y0, x1, y1) fb_line_aa((x0), (y0), (x1), (y1), 28, col)
static void ico_back(int cx, int cy, uint32_t col, int fwd)
{
    int d = fwd ? -1 : 1, X = cx * 16, Y = cy * 16;
    LN(X - d * 72, Y, X + d * 88, Y); LN(X - d * 72, Y, X + d * 8, Y - 80); LN(X - d * 72, Y, X + d * 8, Y + 80);
}
static void ico_reload(int cx, int cy, uint32_t col)
{
    /* arco de 300 grados (tabla de seno/coseno x100) y punta de flecha */
    static const signed char cs[13][2] = { {50,-87}, {87,-50}, {100,0}, {87,50}, {50,87}, {0,100}, {-50,87}, {-87,50}, {-100,0}, {-87,-50}, {-50,-87}, {0,-100}, {26,-97} };
    int X = cx * 16, Y = cy * 16, r = 104;
    for (int i = 1; i + 1 < 12; i++) LN(X + cs[i][0] * r / 100, Y + cs[i][1] * r / 100, X + cs[i + 1][0] * r / 100, Y + cs[i + 1][1] * r / 100);
    LN(X + 56, Y - r, X - 8, Y - r - 58); LN(X + 56, Y - r, X - 8, Y - r + 58);
}
static void ico_stop(int cx, int cy, uint32_t col) { int X = cx * 16, Y = cy * 16; LN(X - 80, Y - 80, X + 80, Y + 80); LN(X - 80, Y + 80, X + 80, Y - 80); }
static void ico_home(int cx, int cy, uint32_t col)
{
    int X = cx * 16, Y = cy * 16;
    LN(X - 128, Y - 8, X, Y - 128); LN(X, Y - 128, X + 128, Y - 8);
    LN(X - 92, Y - 30, X - 92, Y + 112); LN(X - 92, Y + 112, X + 92, Y + 112); LN(X + 92, Y + 112, X + 92, Y - 30);
}
static void ico_plus(int cx, int cy, uint32_t col) { int X = cx * 16, Y = cy * 16; LN(X - 80, Y, X + 80, Y); LN(X, Y - 80, X, Y + 80); }
static void ico_lock(int x, int cy, uint32_t col)
{
    fb_fill_rrect_aa(x, cy - 2, 12, 9, 2, col);
    int X = (x + 6) * 16, Y = (cy - 2) * 16;
    fb_line_aa(X - 56, Y, X - 56, Y - 56, 24, col); fb_line_aa(X + 56, Y, X + 56, Y - 56, 24, col); fb_line_aa(X - 56, Y - 56, X - 24, Y - 96, 24, col);
    fb_line_aa(X - 24, Y - 96, X + 24, Y - 96, 24, col); fb_line_aa(X + 24, Y - 96, X + 56, Y - 56, 24, col);
}
static void ico_globe(int x, int cy, uint32_t col)
{
    static const signed char cs[12][2] = { {100,0}, {87,50}, {50,87}, {0,100}, {-50,87}, {-87,50}, {-100,0}, {-87,-50}, {-50,-87}, {0,-100}, {50,-87}, {87,-50} };
    int X = (x + 6) * 16, Y = cy * 16, r = 96;
    for (int i = 0; i < 12; i++) { int j = (i + 1) % 12; fb_line_aa(X + cs[i][0] * r / 100, Y + cs[i][1] * r / 100, X + cs[j][0] * r / 100, Y + cs[j][1] * r / 100, 20, col); }
    fb_line_aa(X - r, Y, X + r, Y, 18, col); fb_line_aa(X, Y - r, X, Y + r, 18, col);
}
#undef LN
static void nav_button(int i, int x, int y, int enabled)
{
    if (enabled && s_hover_ui == i) fb_fill_rounded_rect(x, y, NAVB, NAVB, NAVB / 2, C_HOVER);
}
static void set_cursor(int c) { if (c != s_cursor_cur) { s_cursor_cur = c; mouse_set_cursor(c); } }

static void page_title(char *o, int max)
{
    struct wpage *p = s_pg;
    if (p->title[0]) wu_copy(o, p->title, (uint32_t)max); else if (p->url[0]) wu_copy(o, p->url, (uint32_t)max); else wu_copy(o, "Nueva pesta\xc3\xb1" "a", (uint32_t)max);
}

static int sb_geom(struct geo *g, int *th, int *tr);
static void draw_scrollbar(struct geo *g)
{
    struct wpage *p = s_pg; int x = g->vx + g->vw - SB_W, y = g->vy, h = g->vh;
    fb_fill_rect((uint32_t)x, (uint32_t)y, SB_W, (uint32_t)h, 0x00F1F3F5);
    fb_fill_rect((uint32_t)x, (uint32_t)y, 1, (uint32_t)h, 0x00E2E5EA);
    int th, tr, range = sb_geom(g, &th, &tr);
    if (range > 0) {
        int off = (int)((int64_t)(tr - th) * p->sy / range);
        fb_fill_rrect_aa(x + 3, y + 2 + off, SB_W - 5, th, 3, s_sb_drag ? 0xFF64748Bu : 0xFFB4BCC8u);
    }
}

static void draw_dropdown(struct geo *g)
{
    struct wpage *p = s_pg; struct wnode *sel = p->dd_sel; if (!sel || !p->lay) return;
    int x, y, w, h; if (!wl_node_rect(p->lay, sel, &x, &y, &w, &h)) return;
    int cnt = wl_select_count(sel), cur = wl_select_index(sel); if (cnt <= 0) return;
    int vis = cnt < 14 ? cnt : 14, first = cur - 6; if (first > cnt - vis) first = cnt - vis; if (first < 0) first = 0;
    int sx0 = g->vx + x - p->sx, sy0 = g->vy + y - p->sy + h, ww = w < 120 ? 120 : w;
    if (sx0 + ww > g->vx + g->vw - SB_W) { sx0 = g->vx + g->vw - SB_W - ww; }
    if (sx0 < g->vx) { sx0 = g->vx; }
    if (sy0 + vis * 20 > g->vy + g->vh) { sy0 = g->vy + g->vh - vis * 20; }
    if (sy0 < g->vy) { sy0 = g->vy; }
    fb_fill_rect((uint32_t)sx0, (uint32_t)sy0, (uint32_t)ww, (uint32_t)(vis * 20), 0x00FFFFFF);
    fb_draw_rect((uint32_t)sx0, (uint32_t)sy0, (uint32_t)ww, (uint32_t)(vis * 20), 0x00475569);
    for (int i = 0; i < vis; i++) {
        int idx = first + i, hl = (s_dd_hl >= 0 ? s_dd_hl : cur) == idx;
        if (hl) fb_fill_rect((uint32_t)(sx0 + 1), (uint32_t)(sy0 + i * 20 + 1), (uint32_t)(ww - 2), 18, 0x003B82F6);
        struct wnode *o = wl_select_option(sel, idx); struct wb b = {0, 0, 0, 0};
        if (o) wd_text_content(o, &b);
        if (b.p) { int n = fit(b.p, ww - 12, 13, 0); tx(sx0 + 6, sy0 + i * 20 + 2, b.p, n, 13, hl ? 0x00FFFFFF : 0x00111827, 0); }
        wb_free(&b);
    }
}

struct paint_args { struct wpage *p; struct geo *g; };
static void paint_thunk(void *v)
{
    struct paint_args *a = (struct paint_args *)v;
    wp_paint(a->p, a->g->vx, a->g->vy, a->g->vw - SB_W, a->g->vh);
    draw_dropdown(a->g);
}

/* Instantanea de la zona de pagina: mientras el hilo trabajador esta maquetando (p->lay_busy) no se toca el arbol de cajas;
 * se repinta la ultima imagen completa y la GUI sigue fluida. */
static uint32_t *s_snap; static int s_snap_w, s_snap_h, s_snap_cap;
static void snap_take(int x, int y, int w, int h)
{
    int fw = (int)fb_get_width(), fh = (int)fb_get_height();
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > fw || y + h > fh) return;
    if (w * h > s_snap_cap) {
        if (s_snap) kfree(s_snap);
        s_snap = (uint32_t *)kmalloc((uint32_t)w * (uint32_t)h * 4u); s_snap_cap = s_snap ? w * h : 0; s_snap_w = s_snap_h = 0;
        if (!s_snap) return;
    }
    volatile uint32_t *fbp = fb_get_buffer();
    for (int j = 0; j < h; j++) { const volatile uint32_t *src = fbp + (size_t)(y + j) * (size_t)fw + (size_t)x; uint32_t *dst = s_snap + (size_t)j * (size_t)w; for (int i = 0; i < w; i++) dst[i] = src[i]; }
    s_snap_w = w; s_snap_h = h;
}
static void snap_show(int x, int y, int w, int h)
{
    if (s_snap && s_snap_w == w && s_snap_h == h) fb_blit(x, y, s_snap, w, h, w);
}
/* SOMA_PARCHE_028_UI: huella de todo lo que cambia el aspecto de la pagina. Si no cambia, el fotograma reutiliza la
 * imagen anterior (una copia de memoria) en vez de volver a pintar el arbol de cajas y reescalar cada foto. */
static uint64_t s_snap_key; static int s_snap_ok;
static uint64_t page_key(struct wpage *p, int w, int h)
{
    uint64_t k = 1469598103934665603ull;
    #define MIX(v) do { k ^= (uint64_t)(v); k *= 1099511628211ull; } while (0)
    MIX(p->gen); MIX((uintptr_t)p->lay); MIX(p->sx); MIX(p->sy); MIX(w); MIX(h); MIX(p->caret_on); MIX((uintptr_t)p->focus);
    MIX((uintptr_t)p->hover); MIX((uintptr_t)p->dd_sel); MIX(s_dd_hl); MIX(p->img_ok); MIX(p->img_fail); MIX(p->nimg);
    if (p->focus) { MIX((uintptr_t)p->focus->value); MIX(p->focus->sel); MIX(p->focus->flags); }
    #undef MIX
    return k;
}

void browser_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor)
{
    ensure_init(); if (!s_pg) return;
    struct wpage *p = s_pg; int wx = (int)win_x, wy = (int)win_y, ww = (int)win_w, wh = (int)win_h;
    s_win_x = win_x; s_win_y = win_y; s_win_w = win_w; s_win_h = win_h;
    s_last_render = web_now_ms();
    start_worker();
    if (s_need_home) { s_need_home = 0; q_push(J_HOME, 0, 0, 0, 0); }
    if (!s_font) s_font = wf_init();
    struct geo g; geo_of(&g, wx, wy, ww, wh);
    wp_set_viewport(p, g.vw - SB_W, g.vh);

    fb_reset_clip();
    fb_fill_rect(win_x, win_y, win_w, (uint32_t)(g.vy - wy), C_BG);

    /* pestanas */
    int tabw = tab_width(ww);
    fb_set_clip(wx, g.tab_y, ww, TAB_H);
    for (int i = 0; i < s_ntabs; i++) {
        int x = wx + 10 + i * tabw, cur = i == s_cur; char ti[96];
        if (cur) page_title(ti, sizeof ti); else wu_copy(ti, s_tabs[i]->title[0] ? s_tabs[i]->title : s_tabs[i]->url, sizeof ti);
        if (cur) { fb_fill_rounded_rect(x, g.tab_y + 6, tabw - 4, TAB_H + 4, 9, C_PANEL); }
        else if (s_hover_ui == 10 + i || s_hover_ui == 30 + i) fb_fill_rounded_rect(x, g.tab_y + 6, tabw - 4, TAB_H - 10, 8, 0x00161B28);
        else if (i + 1 != s_cur && i + 1 < s_ntabs) fb_fill_rect((uint32_t)(x + tabw - 3), (uint32_t)(g.tab_y + 12), 1, TAB_H - 20, C_LINE);
        int busy_tab = cur && (s_job_busy || p->phase != WP_IDLE);
        ico_globe(x + 9, g.tab_y + 6 + (TAB_H - 6) / 2, busy_tab ? (0xFF000000u | C_CYA) : (cur ? 0xFF8A97ADu : 0xFF55627Au));
        int n = fit(ti, tabw - 58, 12, 0); tx(x + 28, g.tab_y + 12, ti, n, 12, cur ? C_TXT : C_DIM, 0);
        int cxm = x + tabw - 18, cym = g.tab_y + 6 + (TAB_H - 6) / 2;
        if (s_hover_ui == 30 + i) fb_fill_rounded_rect(cxm - 9, cym - 9, 18, 18, 9, cur ? C_HOVER : 0x00232B3D);
        { uint32_t col = s_hover_ui == 30 + i ? 0xFFFFFFFFu : 0xFF7C8AA0u; fb_line_aa(cxm * 16 - 52, cym * 16 - 52, cxm * 16 + 52, cym * 16 + 52, 22, col); fb_line_aa(cxm * 16 - 52, cym * 16 + 52, cxm * 16 + 52, cym * 16 - 52, 22, col); }
    }
    if (s_ntabs < MAX_TABS) {
        int x = wx + 10 + s_ntabs * tabw + 2, cym = g.tab_y + 6 + (TAB_H - 6) / 2;
        if (s_hover_ui == 50) fb_fill_rounded_rect(x, cym - 12, 24, 24, 12, C_HOVER);
        ico_plus(x + 12, cym, s_hover_ui == 50 ? 0xFFFFFFFFu : 0xFF8A97ADu);
    }
    fb_reset_clip();

    /* navegacion */
    fb_fill_rect(win_x, (uint32_t)g.nav_y, win_w, NAV_H, C_PANEL);
    int nb = wx + 10, ny = g.nav_y + (NAV_H - NAVB) / 2;
    int busy = s_job_busy || p->phase != WP_IDLE;
    int canb = wp_can_back(p), canf = wp_can_fwd(p);
    nav_button(0, nb, ny, canb); ico_back(nb + NAVB / 2, ny + NAVB / 2, canb ? 0xFFE2E8F0u : 0xFF475569u, 0);
    nav_button(1, nb + NAVSTEP, ny, canf); ico_back(nb + NAVSTEP + NAVB / 2, ny + NAVB / 2, canf ? 0xFFE2E8F0u : 0xFF475569u, 1);
    nav_button(2, nb + 2 * NAVSTEP, ny, 1); if (busy) ico_stop(nb + 2 * NAVSTEP + NAVB / 2, ny + NAVB / 2, 0xFFE2E8F0u); else ico_reload(nb + 2 * NAVSTEP + NAVB / 2, ny + NAVB / 2 + 1, 0xFFE2E8F0u);
    nav_button(3, nb + 3 * NAVSTEP, ny, 1); ico_home(nb + 3 * NAVSTEP + NAVB / 2, ny + NAVB / 2, 0xFFE2E8F0u);
    if (!s_url_focus && !wu_streq(s_url, p->url) && p->url[0]) { wu_copy(s_url, p->url, sizeof s_url); s_url_len = (int)wu_strlen(s_url); s_url_cur = s_url_len; }
    fb_fill_rounded_rect(g.ub_x, ny, g.ub_w, NAVB, NAVB / 2, C_FIELD);
    fb_stroke_rrect_aa(g.ub_x, ny, g.ub_w, NAVB, NAVB / 2, s_url_focus ? (0xFF000000u | C_CYA) : 0xFF2A3446u);
    {
        int secure = wu_startsi(s_url, "https://");
        if (secure) ico_lock(g.ub_x + 14, ny + NAVB / 2 + 1, 0xFF10B981u); else ico_globe(g.ub_x + 14, ny + NAVB / 2, 0xFF8A97ADu);
        int tx0 = g.ub_x + 36, inw = g.ub_w - 36 - 40, off = 0;
        fb_set_clip(tx0, ny + 2, inw, NAVB - 4);
        int cw = tx_w(s_url, s_url_cur, 14, 0);
        if (cw > inw - 4) off = cw - (inw - 4);
        if (s_url_focus && s_url_all && s_url_len) fb_fill_rounded_rect(tx0 - 2, ny + 6, (tx_w(s_url, s_url_len, 14, 0) > inw ? inw : tx_w(s_url, s_url_len, 14, 0)) + 4, NAVB - 12, 3, 0x00214E86);
        tx(tx0 - off, ny + 8, s_url, s_url_len, 14, C_TXT, 0);
        if (s_url_focus && ((web_now_ms() / 500) & 1)) fb_fill_rect((uint32_t)(tx0 + cw - off), (uint32_t)(ny + 7), 2, NAVB - 14, C_CYA);
        fb_reset_clip();
        /* boton "ir" dentro del campo */
        int gx = g.ub_x + g.ub_w - 30, gy = ny + 4;
        fb_fill_rounded_rect(gx, gy, 24, 24, 12, s_hover_ui == 4 ? 0x000E7FD6 : (s_url_focus ? 0x000B6BCB : 0x00222B3D));
        ico_back(gx + 12, gy + 12, 0xFFFFFFFFu, 1);
    }
    fb_fill_rect((uint32_t)g.vx, (uint32_t)(g.vy - 2), (uint32_t)g.vw, 2, 0x00141926);
    if (busy) { int pw = (g.vw * (p->progress < 5 ? 5 : p->progress > 100 ? 100 : p->progress)) / 100; fb_fill_rect((uint32_t)g.vx, (uint32_t)(g.vy - 2), (uint32_t)pw, 2, C_CYA); }

    /* contenido */
    fb_fill_rect((uint32_t)g.vx, (uint32_t)g.vy, (uint32_t)g.vw, (uint32_t)g.vh, 0x00FFFFFF);
    fb_set_clip(g.vx, g.vy, g.vw - SB_W, g.vh);
    if (p->lay_busy) snap_show(g.vx, g.vy, g.vw - SB_W, g.vh);
    else {
        uint64_t key = page_key(p, g.vw - SB_W, g.vh);
        if (s_snap_ok && key == s_snap_key && s_snap && s_snap_w == g.vw - SB_W && s_snap_h == g.vh) snap_show(g.vx, g.vy, g.vw - SB_W, g.vh);
        else {
            struct paint_args pa = { p, &g }; on_gui_stack(paint_thunk, &pa);
            s_snap_w = 0; snap_take(g.vx, g.vy, g.vw - SB_W, g.vh);
            s_snap_ok = (s_snap_w == g.vw - SB_W && s_snap_h == g.vh); s_snap_key = key;
        }
    }
    fb_reset_clip();
    draw_scrollbar(&g);

    if (s_find_open) {
        int fw = 260, fx = g.vx + g.vw - SB_W - fw - 6, fy = g.vy + 6;
        fb_draw_shadow(fx, fy, fw, 28, 8, 12, 4, 0x70000000u);
        fb_fill_rounded_rect(fx, fy, fw, 28, 8, C_PANEL); fb_stroke_rrect_aa(fx, fy, fw, 28, 8, 0xFF000000u | C_CYA);
        tx(fx + 8, fy + 6, "Buscar:", 7, 13, C_DIM, 0); tx(fx + 66, fy + 6, s_find, s_find_len, 13, C_TXT, 0);
        int cx = fx + 66 + tx_w(s_find, s_find_len, 13, 0); if ((web_now_ms() / 500) & 1) fb_fill_rect((uint32_t)cx, (uint32_t)(fy + 5), 2, 17, C_AMB);
    }
    if (p->alert_pending) {
        int aw = ww > 460 ? 420 : ww - 40, ax = wx + (ww - aw) / 2, ay = wy + wh / 2 - 50;
        fb_fill_rect_alpha(g.vx, g.vy, g.vw, g.vh, 0x60000000u);
        fb_draw_shadow(ax, ay, aw, 100, 10, 22, 8, 0xA0000000u);
        fb_fill_rounded_rect(ax, ay, aw, 100, 10, C_PANEL); fb_stroke_rrect_aa(ax, ay, aw, 100, 10, 0xFF3A465Cu);
        tx(ax + 12, ay + 8, "Esta p\xc3\xa1gina dice", 17, 13, C_DIM, WF_BOLD);
        int n = fit(p->alert_msg, aw - 24, 14, 0); tx(ax + 12, ay + 34, p->alert_msg, n, 14, C_TXT, 0);
        button(ax + aw - 92, ay + 62, 80, 28, "Aceptar", C_GRN, 1);
    }

    /* linea de estado: enlace bajo el puntero, progreso de la carga o errores de red (los avisos de script no se muestran) */
    fb_fill_rect(win_x, (uint32_t)g.stat_y, win_w, STAT_H, C_BG);
    fb_fill_rect(win_x, (uint32_t)g.stat_y, win_w, 1, 0x00141926);
    {
        const char *st = s_hover_href[0] ? s_hover_href : p->status; uint32_t col = s_hover_href[0] ? C_DIM : p->status_kind == WPS_ERR ? C_AMB : p->status_kind == WPS_BUSY ? C_CYA : 0x0064748B;
        fb_set_clip(wx, g.stat_y, ww, STAT_H);
        int n = fit(st, ww - 60, 12, 0); tx(wx + 12, g.stat_y + 4, st, n, 12, col, 0);
        if (busy) { int k = (int)((web_now_ms() / 90) % 8); for (int i = 0; i < 3; i++) fb_fill_rrect_aa(wx + ww - 40 + i * 9, g.stat_y + STAT_H / 2 - 2, 5, 5, 2, (0xFF000000u | C_CYA) - (((k + i * 2) % 8) > 3 ? 0x90000000u : 0u)); }
        fb_reset_clip();
    }
    (void)show_cursor;
}

/* ------------------------------------------------------------ entrada */
static void url_set_from_edit(void) { s_url[s_url_len] = 0; }
static void url_go(void)
{
    url_set_from_edit(); s_url_focus = 0; s_url_all = 0;
    if (s_url_len > 0) q_push(J_LOAD, 0, 0, 0, s_url);
}
static void url_insert(const char *b, int n)
{
    if (s_url_all) { s_url_len = 0; s_url_cur = 0; s_url_all = 0; }
    if (s_url_len + n >= (int)sizeof s_url - 1) return;
    for (int i = s_url_len; i >= s_url_cur; i--) s_url[i + n] = s_url[i];
    memcpy(s_url + s_url_cur, b, (uint32_t)n); s_url_len += n; s_url_cur += n; s_url[s_url_len] = 0;
}
static void url_key(int k, const char *u, int un)
{
    if (k == 10 || k == 13) { url_go(); return; }
    if (k == 27) { s_url_focus = 0; s_url_all = 0; return; }
    if (k == 8 || k == 127) {
        if (s_url_all) { s_url_len = s_url_cur = 0; s_url[0] = 0; s_url_all = 0; return; }
        if (s_url_cur > 0) { int a = s_url_cur - 1; while (a > 0 && ((uint8_t)s_url[a] & 0xC0) == 0x80) a--; int n = s_url_cur - a; for (int i = a; i <= s_url_len - n; i++) s_url[i] = s_url[i + n]; s_url_len -= n; s_url_cur = a; }
        return;
    }
    if (k == 4) { if (s_url_all) { s_url_len = s_url_cur = 0; s_url[0] = 0; s_url_all = 0; return; } if (s_url_cur < s_url_len) { int n = 1; while (s_url_cur + n < s_url_len && ((uint8_t)s_url[s_url_cur + n] & 0xC0) == 0x80) n++; for (int i = s_url_cur; i <= s_url_len - n; i++) s_url[i] = s_url[i + n]; s_url_len -= n; } return; }
    if (k == 2) { s_url_all = 0; if (s_url_cur > 0) { s_url_cur--; while (s_url_cur > 0 && ((uint8_t)s_url[s_url_cur] & 0xC0) == 0x80) s_url_cur--; } return; }
    if (k == 6) { s_url_all = 0; if (s_url_cur < s_url_len) { s_url_cur++; while (s_url_cur < s_url_len && ((uint8_t)s_url[s_url_cur] & 0xC0) == 0x80) s_url_cur++; } return; }
    if (k == 1) { s_url_all = 0; s_url_cur = 0; return; }
    if (k == 5) { s_url_all = 0; s_url_cur = s_url_len; return; }
    if (un > 0 && (uint8_t)u[0] >= 32 && k != 127) url_insert(u, un);
}
static void find_key(int k, const char *u, int un)
{
    if (k == 27) { s_find_open = 0; return; }
    if (k == 10 || k == 13) { if (s_find_len) q_push(J_FIND, 0, 0, 0, s_find); return; }
    if (k == 8 || k == 127) { if (s_find_len > 0) { s_find_len--; while (s_find_len > 0 && ((uint8_t)s_find[s_find_len] & 0xC0) == 0x80) s_find_len--; s_find[s_find_len] = 0; } return; }
    if (un > 0 && (uint8_t)u[0] >= 32 && s_find_len + un < (int)sizeof s_find - 1) { memcpy(s_find + s_find_len, u, (uint32_t)un); s_find_len += un; s_find[s_find_len] = 0; }
}

void browser_handle_key(char c)
{
    ensure_init(); if (!s_pg) return;
    s_last_render = web_now_ms();
    int k = (uint8_t)c;
    if (s_pg->alert_pending) { if (k == 10 || k == 13 || k == 27 || k == ' ') s_pg->alert_pending = 0; return; }
    /* ensamblar UTF-8 */
    char u[5]; int un = 0;
    if (k >= 0x80) {
        if (s_u8n == 0) { s_u8need = k >= 0xF0 ? 4 : k >= 0xE0 ? 3 : k >= 0xC0 ? 2 : 0; if (!s_u8need) return; }
        else if ((k & 0xC0) != 0x80) { s_u8n = 0; return; }
        s_u8[s_u8n++] = (uint8_t)k;
        if (s_u8n < s_u8need) return;
        memcpy(u, s_u8, (uint32_t)s_u8n); un = s_u8n; u[un] = 0; s_u8n = 0; k = 0x100;
    } else if (k >= 32 && k != 127) { u[0] = c; u[1] = 0; un = 1; }
    else u[0] = 0;
    /* atajos globales */
    if (k == 12) { s_url_focus = 1; s_url_all = 1; s_url_cur = s_url_len; return; }
    if (k == 20) { q_push(J_NEWTAB, 0, 0, 0, 0); return; }
    if (k == 23) { q_push(J_CLOSE, s_cur, 0, 0, 0); return; }
    if (k == 18) { q_push(J_RELOAD, 0, 0, 0, 0); return; }
    if (s_url_focus) { url_key(k, u, un); return; }
    if (s_find_open) { find_key(k, u, un); return; }
    if (s_pg->dd_sel) {
        int cnt = wl_select_count(s_pg->dd_sel), cur = s_dd_hl >= 0 ? s_dd_hl : wl_select_index(s_pg->dd_sel);
        if (k == 14 && cur + 1 < cnt) { s_dd_hl = cur + 1; return; }
        if (k == 16 && cur > 0) { s_dd_hl = cur - 1; return; }
        if (k == 10 || k == 13) { q_push(J_SELECT, cur, 0, 0, 0); s_dd_hl = -1; return; }
        if (k == 27) { s_pg->dd_sel = 0; s_dd_hl = -1; return; }
    }
    if (k == '/' && !s_pg->focus) { s_find_open = 1; return; }
    if (k == 0x100) { q_push(J_KEY, 0x100, 0, 0, u); return; }
    q_push(J_KEY, k, 0, 0, u);
}

static int sb_geom(struct geo *g, int *th, int *tr)
{
    int doc = wp_doc_height(s_pg), vh = s_pg->vh; *tr = g->vh - 4; if (doc <= vh || *tr < 10) { *th = 0; return 0; }
    *th = (int)((int64_t)*tr * vh / doc); if (*th < 28) *th = 28; if (*th > *tr) *th = *tr; return doc - vh;
}
static void sb_to(struct geo *g, int my)
{
    int th, tr, range = sb_geom(g, &th, &tr); if (!range) return;
    int pos = my - g->vy - 2 - s_sb_grab, span = tr - th; if (span < 1) span = 1;
    if (pos < 0) { pos = 0; }
    if (pos > span) { pos = span; }
    s_pg->sy = (int)((int64_t)range * pos / span);
}

int browser_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    ensure_init(); if (!s_pg) return 0;
    struct wpage *p = s_pg; s_last_render = web_now_ms();
    int wx = (int)win_x, wy = (int)win_y, ww = (int)win_w, wh = (int)win_h; struct geo g; geo_of(&g, wx, wy, ww, wh);
    if (p->alert_pending) {
        int aw = ww > 460 ? 420 : ww - 40, ax = wx + (ww - aw) / 2, ay = wy + wh / 2 - 50;
        if (in_r(mx, my, ax + aw - 92, ay + 62, 80, 28)) p->alert_pending = 0;
        return 0;
    }
    /* pestanas */
    if (my >= g.tab_y && my < g.tab_y + TAB_H) {
        int tabw = tab_width(ww);
        for (int i = 0; i < s_ntabs; i++) {
            int x = wx + 10 + i * tabw;
            if (in_r(mx, my, x, g.tab_y, tabw - 2, TAB_H)) { if (mx >= x + tabw - 30) q_push(J_CLOSE, i, 0, 0, 0); else if (i != s_cur) q_push(J_SWITCH, i, 0, 0, 0); return 0; }
        }
        if (s_ntabs < MAX_TABS && in_r(mx, my, wx + 10 + s_ntabs * tabw, g.tab_y, 30, TAB_H)) q_push(J_NEWTAB, 0, 0, 0, 0);
        return 0;
    }
    int ny = g.nav_y + (NAV_H - NAVB) / 2;
    if (my >= g.nav_y && my < g.nav_y + NAV_H) {
        int nb = wx + 10;
        if (in_r(mx, my, nb, ny, NAVB, NAVB)) { if (wp_can_back(p)) q_push(J_BACK, 0, 0, 0, 0); }
        else if (in_r(mx, my, nb + NAVSTEP, ny, NAVB, NAVB)) { if (wp_can_fwd(p)) q_push(J_FWD, 0, 0, 0, 0); }
        else if (in_r(mx, my, nb + 2 * NAVSTEP, ny, NAVB, NAVB)) { if (s_job_busy || p->phase != WP_IDLE) p->stop = 1; else q_push(J_RELOAD, 0, 0, 0, 0); }
        else if (in_r(mx, my, nb + 3 * NAVSTEP, ny, NAVB, NAVB)) { q_push(J_HOME, 0, 0, 0, 0); s_url_focus = 0; }
        else if (in_r(mx, my, g.ub_x + g.ub_w - 34, ny, 34, NAVB)) url_go();
        else if (in_r(mx, my, g.ub_x, ny, g.ub_w, NAVB)) {
            if (!s_url_focus) { s_url_focus = 1; s_url_all = 1; s_url_cur = s_url_len; }
            else { s_url_all = 0; int best = 0, bd = 1 << 30; int inw = g.ub_w - 36 - 40, cwc = tx_w(s_url, s_url_cur, 14, 0), off = cwc > inw - 4 ? cwc - (inw - 4) : 0; int rel = mx - (g.ub_x + 36) + off;
                for (int i = 0; i <= s_url_len; i++) { if (i < s_url_len && ((uint8_t)s_url[i] & 0xC0) == 0x80) continue; int d = tx_w(s_url, i, 14, 0) - rel; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } }
                s_url_cur = best; }
        }
        return 0;
    }
    s_url_focus = 0; s_url_all = 0;
    if (!in_r(mx, my, g.vx, g.vy, g.vw, g.vh)) return 0;
    /* barra de desplazamiento */
    if (mx >= g.vx + g.vw - SB_W) {
        int th, tr, range = sb_geom(&g, &th, &tr);
        if (range) {
            int off = (int)((int64_t)(tr - th) * p->sy / range), ty = g.vy + 2 + off;
            if (my >= ty && my < ty + th) { s_sb_drag = 1; s_sb_grab = my - ty; }
            else wp_scroll_by(p, 0, my < ty ? -(p->vh - 40) : p->vh - 40);
        }
        return 0;
    }
    if (p->dd_sel && p->lay) {
        int x, y, w, h; struct wnode *sel = p->dd_sel;
        if (wl_node_rect(p->lay, sel, &x, &y, &w, &h)) {
            int cnt = wl_select_count(sel), cur = wl_select_index(sel), vis = cnt < 14 ? cnt : 14, first = cur - 6; if (first > cnt - vis) first = cnt - vis; if (first < 0) first = 0;
            int ww2 = w < 120 ? 120 : w, sx0 = g.vx + x - p->sx, sy0 = g.vy + y - p->sy + h;
            if (sx0 + ww2 > g.vx + g.vw - SB_W) { sx0 = g.vx + g.vw - SB_W - ww2; }
            if (sx0 < g.vx) { sx0 = g.vx; }
            if (sy0 + vis * 20 > g.vy + g.vh) { sy0 = g.vy + g.vh - vis * 20; }
            if (sy0 < g.vy) { sy0 = g.vy; }
            if (in_r(mx, my, sx0, sy0, ww2, vis * 20)) { q_push(J_SELECT, first + (my - sy0) / 20, 0, 0, 0); s_dd_hl = -1; return 0; }
        }
        s_dd_hl = -1;
    }
    uint64_t now = web_now_ms(); int dbl = 0;
    if (now - s_last_click_t < 400 && (mx - s_last_cx) * (mx - s_last_cx) + (my - s_last_cy) * (my - s_last_cy) < 36) dbl = 1;
    s_last_click_t = dbl ? 0 : now; s_last_cx = mx; s_last_cy = my;
    q_push(J_CLICK, mx - g.vx + p->sx, my - g.vy + p->sy, 0 | (dbl << 8), 0);
    return 0;
}

struct hover_args { struct wpage *p; int px, py; int k; struct wnode *link; };
static void hover_thunk(void *v)
{
    struct hover_args *a = (struct hover_args *)v;
    a->k = wp_hit_cursor(a->p, a->px, a->py);
    a->link = 0; wl_hit(a->p->lay, a->px, a->py, &a->link, 0);
}

/* Gancho de raton para hover, rueda y arrastre de la barra (lo llama gui.c si existe; sin el, el navegador sigue funcionando) */
void browser_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int held)
{
    if (!s_inited || !s_pg) return;
    static int32_t lx = -1, ly = -1; static uint32_t lsy = 0xFFFFFFFFu;
    if (mx == lx && my == ly && !wheel && !held && !s_sb_drag && (uint32_t)s_pg->sy == lsy) return;
    lx = mx; ly = my; lsy = (uint32_t)s_pg->sy;
    struct wpage *p = s_pg; struct geo g; geo_of(&g, (int)win_x, (int)win_y, (int)win_w, (int)win_h);
    {   /* SOMA_PARCHE_028_UI: resalte de los botones y pestanas bajo el puntero */
        int wx = (int)win_x, ww = (int)win_w, h = -1, ny = g.nav_y + (NAV_H - NAVB) / 2, nb = wx + 10;
        if (my >= g.tab_y && my < g.tab_y + TAB_H) {
            int tabw = tab_width(ww);
            for (int i = 0; i < s_ntabs; i++) { int x = wx + 10 + i * tabw; if (in_r(mx, my, x, g.tab_y, tabw - 2, TAB_H)) h = (mx >= x + tabw - 30 ? 30 : 10) + i; }
            if (h < 0 && s_ntabs < MAX_TABS && in_r(mx, my, wx + 10 + s_ntabs * tabw, g.tab_y, 30, TAB_H)) h = 50;
        } else if (my >= g.nav_y && my < g.nav_y + NAV_H) {
            for (int i = 0; i < 4; i++) if (in_r(mx, my, nb + i * NAVSTEP, ny, NAVB, NAVB)) h = i;
            if (in_r(mx, my, g.ub_x + g.ub_w - 34, ny, 34, NAVB)) h = 4;
        }
        s_hover_ui = held ? -1 : h;
    }
    int inview = in_r(mx, my, g.vx, g.vy, g.vw - SB_W, g.vh) && !p->alert_pending;
    if (s_sb_drag) { if (held) sb_to(&g, my); else s_sb_drag = 0; }
    if (wheel && in_r(mx, my, (int)win_x, g.vy, (int)win_w, g.vh) && !s_sb_drag) { wp_scroll_by(p, 0, -wheel * 48); s_last_render = web_now_ms(); }
    if (inview && p->lay && !p->lay_busy) {
        int px = mx - g.vx + p->sx, py = my - g.vy + p->sy;
        s_move_x = mx - g.vx; s_move_y = my - g.vy; s_move_pending = 1;
        struct hover_args ha = { p, px, py, 0, 0 }; on_gui_stack(hover_thunk, &ha);
        set_cursor(ha.k == WPK_LINK ? CURSOR_HAND : CURSOR_ARROW);
        const char *h = ha.link ? wd_attr(ha.link, "href") : 0;
        if (h && h[0]) wu_copy(s_hover_href, h, sizeof s_hover_href); else s_hover_href[0] = 0;
    } else { if (s_hover_href[0]) s_hover_href[0] = 0; set_cursor(CURSOR_ARROW); }
}

/* ------------------------------------------------------------ API publica (compatible con la anterior) */
void browser_init(void) { ensure_init(); }
void browser_load_url(const char *url)
{
    ensure_init(); if (!s_pg || !url || !*url) return;
    s_need_home = 0;
    wu_copy(s_url, url, sizeof s_url); s_url_len = (int)wu_strlen(s_url); s_url_cur = s_url_len;
    q_push(J_LOAD, 0, 0, 0, url);
}
int browser_is_loading(void)
{
    if (!s_inited || !s_pg) return 0;
    return s_job_busy || s_qh != s_qt || s_pg->phase == WP_FETCH || s_pg->phase == WP_RES || s_pg->phase == WP_SCRIPT;
}
struct text_args { struct wdoc *d; struct wnode *n; const char *s; struct wb b; };
static void settext_thunk(void *v) { struct text_args *a = (struct text_args *)v; wd_set_text(a->d, a->n, a->s); }
static void gettext_thunk(void *v) { struct text_args *a = (struct text_args *)v; wd_text_content(a->n, &a->b); }
int browser_set_element_text_by_id(const char *id, const char *new_text)
{
    if (!s_inited || !s_pg || !s_pg->doc || !id || s_pg->lay_busy) return 0;
    struct wnode *n = wd_by_id(s_pg->doc->root, id); if (!n) return 0;
    struct text_args ta; memset(&ta, 0, sizeof ta); ta.d = s_pg->doc; ta.n = n; ta.s = new_text ? new_text : ""; on_gui_stack(settext_thunk, &ta);
    s_pg->need_style = 1; return 1;
}
const char *browser_get_element_text_by_id(const char *id)
{
    static char buf[256];
    buf[0] = 0;
    if (!s_inited || !s_pg || !s_pg->doc || !id || s_pg->lay_busy) return buf;
    struct wnode *n = wd_by_id(s_pg->doc->root, id); if (!n) return buf;
    struct text_args ta; memset(&ta, 0, sizeof ta); ta.d = s_pg->doc; ta.n = n; on_gui_stack(gettext_thunk, &ta);
    if (ta.b.p) wu_copy(buf, ta.b.p, sizeof buf);
    wb_free(&ta.b); return buf;
}
void browser_js_eval(const char *code)
{
    ensure_init(); if (!s_pg || !code || !*code) return;
    q_push(J_EVAL, 0, 0, 0, code);
}
