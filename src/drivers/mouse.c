#include "mouse.h"
#include "fb.h"
#include "io.h"
#include "arch.h"
#include "cursors/cursor_arrow.h"
#include "cursors/cursor_crosshair.h"
#include "cursors/cursor_hand.h"

#define MAX_CURSOR_DIM 32
static uint32_t cursor_argb[MAX_CURSOR_DIM * MAX_CURSOR_DIM];

struct soma_cursor_entry {
    const char *name;
    const char *desc;
    uint32_t width;
    uint32_t height;
    const uint8_t *data;
};

/* Mapas de bits de siempre (la mano y la cruz); el resto se dibuja con formas vectoriales (PARCHE 085) */
static const struct soma_cursor_entry CURSOR_REGISTRY[CURSOR_COUNT] = {
    [CURSOR_ARROW]       = { "arrow",     "Flecha (vectorial, suavizada)",       CURSOR_ARROW_W, CURSOR_ARROW_H, (const uint8_t *)cursor_arrow_data },
    [CURSOR_CROSSHAIR]   = { "crosshair", "Cruz de seleccion y diseno",          CURSOR_CROSS_W, CURSOR_CROSS_H, (const uint8_t *)cursor_cross_data },
    [CURSOR_HAND]        = { "hand",      "Mano de navegacion y enlaces",        CURSOR_HAND_W,  CURSOR_HAND_H,  (const uint8_t *)cursor_hand_data },
    [CURSOR_RESIZE_H]    = { "resize-h",  "Redimensionar en horizontal",         0, 0, 0 },
    [CURSOR_RESIZE_V]    = { "resize-v",  "Redimensionar en vertical",           0, 0, 0 },
    [CURSOR_RESIZE_NWSE] = { "resize-nwse", "Redimensionar en diagonal (\\)",   0, 0, 0 },
    [CURSOR_RESIZE_NESW] = { "resize-nesw", "Redimensionar en diagonal (/)",    0, 0, 0 },
    [CURSOR_MOVE]        = { "move",      "Mover",                               0, 0, 0 },
    [CURSOR_TEXT]        = { "text",      "Texto (I)",                           0, 0, 0 },
};

/* ---------------------------------------------------------------- PARCHE 085: punteros vectoriales
 * Cada forma es uno o varios poligonos en 1/4 de pixel. Se rasteriza a 4x4 submuestras por pixel (relleno), se dilata 1,25 px para
 * el borde negro y se anade una sombra suave desplazada. Resultado ARGB con alfa (fb_present ya mezcla el puntero con el fondo). */
#define CS 24                    /* lado del puntero */
#define SS 4                     /* submuestras por pixel y eje */
#define GW (CS * SS)
static uint8_t g_fill[GW * GW], g_tmp[GW * GW];
struct cpoly { int n; int16_t v[16][2]; };      /* vertices en 1/4 px */
static int in_poly(const struct cpoly *p, int x, int y)   /* x, y: centro de submuestra en 1/8 px (2x+1) */
{
    int in = 0;
    for (int i = 0, j = p->n - 1; i < p->n; j = i++) {
        int xi = p->v[i][0] * 2, yi = p->v[i][1] * 2, xj = p->v[j][0] * 2, yj = p->v[j][1] * 2;
        if ((yi > y) != (yj > y)) {
            int64_t lhs = (int64_t)(x - xi) * (yj - yi), rhs = (int64_t)(xj - xi) * (y - yi);
            if ((yj - yi) > 0 ? lhs < rhs : lhs > rhs) in = !in;
        }
    }
    return in;
}
static void raster(const struct cpoly *ps, int np)
{
    for (int y = 0; y < GW; y++) for (int x = 0; x < GW; x++) {
        int v = 0;
        for (int k = 0; k < np && !v; k++) v = in_poly(&ps[k], 2 * x + 1, 2 * y + 1);
        g_fill[y * GW + x] = (uint8_t)v;
    }
}
static void dilate(const uint8_t *src, uint8_t *dst, int r)
{
    for (int y = 0; y < GW; y++) for (int x = 0; x < GW; x++) {
        int v = 0;
        for (int dy = -r; dy <= r && !v; dy++) for (int dx = -r; dx <= r && !v; dx++) {
            if (dx * dx + dy * dy > r * r + r) continue;
            int xx = x + dx, yy = y + dy;
            if (xx >= 0 && yy >= 0 && xx < GW && yy < GW && src[yy * GW + xx]) v = 1;
        }
        dst[y * GW + x] = (uint8_t)v;
    }
}
static uint32_t g_vec[CURSOR_COUNT][CS * CS]; static uint8_t g_vec_ok[CURSOR_COUNT];
static int cov(const uint8_t *m, int px, int py, int ox, int oy)   /* cobertura 0..16 del pixel desplazado en submuestras */
{
    int c = 0;
    for (int sy = 0; sy < SS; sy++) for (int sx = 0; sx < SS; sx++) {
        int x = px * SS + sx - ox, y = py * SS + sy - oy;
        if (x >= 0 && y >= 0 && x < GW && y < GW && m[y * GW + x]) c++;
    }
    return c;
}
static void compose(uint32_t *out)
{
    dilate(g_fill, g_tmp, 5);                                      /* borde de 1,25 px */
    for (int y = 0; y < CS; y++) for (int x = 0; x < CS; x++) {
        int f = cov(g_fill, x, y, 0, 0), o = cov(g_tmp, x, y, 0, 0);
        int sh = 0;                                                /* sombra: borde desplazado 1,5 px y difuminado */
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) sh += cov(g_tmp, x + dx, y + dy, 6, 6);
        sh = sh * 90 / (16 * 9);                                   /* alfa maxima ~90 */
        /* capas: sombra -> borde negro -> relleno blanco */
        uint32_t a = (uint32_t)sh, r = 0, g = 0, b = 0;
        uint32_t ao = (uint32_t)(o * 255 / 16);
        a = ao + a * (255 - ao) / 255;                              /* negro sobre sombra (ambos negros) */
        uint32_t af = (uint32_t)(f * 255 / 16);
        uint32_t A = af + a * (255 - af) / 255;
        if (A) { uint32_t w = af * 255 / A; r = g = b = w; }     /* blanco premultiplicado entre lo negro */
        out[y * CS + x] = A ? (A << 24 | r << 16 | g << 8 | b) : 0;
    }
}
#define Q(x) ((int16_t)((x) * 4))
static void rot45(struct cpoly *p, int sign)                       /* gira 45 grados alrededor del centro (12,12) */
{
    for (int i = 0; i < p->n; i++) {
        int x = p->v[i][0] - Q(12), y = p->v[i][1] - Q(12);
        int nx = (x * 181 - sign * y * 181) / 256, ny = (sign * x * 181 + y * 181) / 256;
        p->v[i][0] = (int16_t)(nx + Q(12)); p->v[i][1] = (int16_t)(ny + Q(12));
    }
}
static void transpose(struct cpoly *p) { for (int i = 0; i < p->n; i++) { int16_t t = p->v[i][0]; p->v[i][0] = p->v[i][1]; p->v[i][1] = t; } }
static const struct cpoly ARROW = { 7, { { Q(2), Q(1) }, { Q(2), Q(18) }, { Q(6), Q(14) }, { Q(9), Q(21) }, { Q(12), Q(19) + 2 }, { Q(9) + 1, Q(13) }, { Q(14), Q(13) } } };
static const struct cpoly DOUBLE_H = { 10, { { Q(1), Q(12) }, { Q(6), Q(7) }, { Q(6), Q(10) }, { Q(18), Q(10) }, { Q(18), Q(7) }, { Q(23), Q(12) },
                                              { Q(18), Q(17) }, { Q(18), Q(14) }, { Q(6), Q(14) }, { Q(6), Q(17) } } };
static const struct cpoly CROSS_H = { 10, { { Q(1), Q(12) }, { Q(5), Q(8) }, { Q(5), Q(11) }, { Q(19), Q(11) }, { Q(19), Q(8) }, { Q(23), Q(12) },
                                             { Q(19), Q(16) }, { Q(19), Q(13) }, { Q(5), Q(13) }, { Q(5), Q(16) } } };
static int g_hot[CURSOR_COUNT][2];
static void build_vec(int id)
{
    struct cpoly p[3]; int n = 0;
    switch (id) {
    case CURSOR_ARROW: p[0] = ARROW; n = 1; g_hot[id][0] = 2; g_hot[id][1] = 1; break;
    case CURSOR_RESIZE_H: p[0] = DOUBLE_H; n = 1; break;
    case CURSOR_RESIZE_V: p[0] = DOUBLE_H; transpose(&p[0]); n = 1; break;
    case CURSOR_RESIZE_NWSE: p[0] = DOUBLE_H; rot45(&p[0], 1); n = 1; break;
    case CURSOR_RESIZE_NESW: p[0] = DOUBLE_H; rot45(&p[0], -1); n = 1; break;
    case CURSOR_MOVE: p[0] = CROSS_H; p[1] = CROSS_H; transpose(&p[1]); n = 2; break;
    case CURSOR_TEXT: {
        static const struct cpoly bar = { 4, { { Q(11), Q(4) }, { Q(13), Q(4) }, { Q(13), Q(20) }, { Q(11), Q(20) } } };
        static const struct cpoly top = { 4, { { Q(8), Q(3) }, { Q(16), Q(3) }, { Q(16), Q(5) }, { Q(8), Q(5) } } };
        static const struct cpoly bot = { 4, { { Q(8), Q(19) }, { Q(16), Q(19) }, { Q(16), Q(21) }, { Q(8), Q(21) } } };
        p[0] = bar; p[1] = top; p[2] = bot; n = 3; break; }
    default: return;
    }
    if (id != CURSOR_ARROW) { g_hot[id][0] = 12; g_hot[id][1] = 12; }
    raster(p, n);
    compose(g_vec[id]);
    g_vec_ok[id] = 1;
}

static int active_cursor_id = CURSOR_ARROW;
static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[4];
static int mouse_intelli = 0;           /* SOMA_PARCHE_024_TERM: IntelliMouse (rueda): paquetes de 4 bytes */
static int mouse_wheel_acc = 0;
static int mouse_right_prev = 0, mouse_right_down = 0, mouse_right_clicks = 0;
static int mouse_left_prev = 0;
static int mouse_left_down = 0;

static void reload_cursor_bitmap(void)
{
    int id = active_cursor_id;
    if (id != CURSOR_HAND && id != CURSOR_CROSSHAIR) {
        if (!g_vec_ok[id]) build_vec(id);
        if (g_vec_ok[id]) { fb_cursor_set_image(g_vec[id], CS, CS); fb_cursor_set_hotspot(g_hot[id][0], g_hot[id][1]); return; }
    }
    const struct soma_cursor_entry *c = &CURSOR_REGISTRY[id];
    uint32_t w = c->width;
    uint32_t h = c->height;

    for (uint32_t r = 0; r < h; ++r) {
        for (uint32_t col = 0; col < w; ++col) {
            uint8_t pix = c->data[r * w + col];
            if (pix == 1) {
                cursor_argb[r * w + col] = 0xFF000000; /* Borde negro opaco */
            } else if (pix == 2) {
                cursor_argb[r * w + col] = 0xFFFFFFFF; /* Relleno blanco opaco */
            } else {
                cursor_argb[r * w + col] = 0x00000000; /* Transparente */
            }
        }
    }
    fb_cursor_set_image(cursor_argb, (int32_t)w, (int32_t)h);
    fb_cursor_set_hotspot(id == CURSOR_CROSSHAIR ? (int32_t)w / 2 : 0, id == CURSOR_CROSSHAIR ? (int32_t)h / 2 : 0);
}

int mouse_set_cursor(int cursor_id)
{
    if (cursor_id < 0 || cursor_id >= CURSOR_COUNT) return 0;
    active_cursor_id = cursor_id;
    reload_cursor_bitmap();
    return 1;
}

int mouse_set_cursor_by_name(const char *name)
{
    if (!name || !*name) return 0;
    for (int i = 0; i < CURSOR_COUNT; ++i) {
        const char *a = name, *b = CURSOR_REGISTRY[i].name;
        int eq = 1;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
            if (ca != cb) { eq = 0; break; }
            a++; b++;
        }
        if (eq && *a == '\0' && *b == '\0') {
            active_cursor_id = i;
            reload_cursor_bitmap();
            return 1;
        }
    }
    return 0;
}

int mouse_get_current_cursor(void)
{
    return active_cursor_id;
}

const char *mouse_get_cursor_name(int cursor_id)
{
    if (cursor_id < 0 || cursor_id >= CURSOR_COUNT) return "";
    return CURSOR_REGISTRY[cursor_id].name;
}

const char *mouse_get_cursor_desc(int cursor_id)
{
    if (cursor_id < 0 || cursor_id >= CURSOR_COUNT) return "";
    return CURSOR_REGISTRY[cursor_id].desc;
}

int mouse_get_cursor_count(void)
{
    return CURSOR_COUNT;
}

void mouse_erase_cursor(void)
{
    fb_cursor_hide();
}

void mouse_draw_cursor(int32_t mx, int32_t my)
{
    fb_cursor_show(mx, my);
}

static void mouse_wait_write(void)
{
    uint32_t timeout = 50000;
    while ((inb(0x64) & 0x02) && --timeout) arch_pause();
}

static int mouse_wait_read(void)
{
    uint32_t timeout = 50000;
    while (!(inb(0x64) & 0x01) && --timeout) arch_pause();
    return (timeout > 0);
}

static void mouse_write_cmd(uint8_t cmd)
{
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, cmd);
}

static uint8_t mouse_read_data(void)
{
    if (!mouse_wait_read()) return 0;
    return inb(0x60);
}

static int mouse_set_rate(uint8_t rate)
{
    mouse_write_cmd(0xF3);
    if (mouse_read_data() != 0xFA) return 0;
    mouse_write_cmd(rate);
    return mouse_read_data() == 0xFA;
}

void mouse_shutdown(void)
{
#ifdef __x86_64__
    mouse_write_cmd(0xF5);
    mouse_read_data();
    mouse_wait_write();
    outb(0x64, 0xA7);
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, 0x65);

    for (int i = 0; i < 2000; ++i) {
        if (inb(0x64) & 0x01) inb(0x60);
        arch_pause();
    }
    mouse_cycle = 0;
#endif
}

void mouse_init(void)
{
#ifdef __x86_64__
    for (int i = 0; i < 2000; ++i) {
        if (inb(0x64) & 0x01) inb(0x60);
        arch_pause();
    }
    mouse_wait_write();
    outb(0x64, 0xA8);
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, 0x45);

    mouse_write_cmd(0xF6);
    mouse_read_data();
    /* SOMA_PARCHE_024_TERM: activa la rueda (secuencia magica de IntelliMouse: tasas 200, 100, 80 y pedir el ID).
     * Si el raton no la tiene (ID 0), todo sigue con paquetes de 3 bytes. */
    mouse_intelli = 0;
    if (mouse_set_rate(200) && mouse_set_rate(100) && mouse_set_rate(80)) {
        mouse_write_cmd(0xF2);
        if (mouse_read_data() == 0xFA && mouse_read_data() == 3) mouse_intelli = 1;
    }
    mouse_write_cmd(0xF4);
    mouse_read_data();
    mouse_cycle = 0;

    reload_cursor_bitmap();
#else
    reload_cursor_bitmap();                          /* PARCHE 064: sin PS/2 solo hace falta la imagen del cursor (el raton llega por USB) */
#endif
}

static int mouse_finish_packet(int32_t *out_dx, int32_t *out_dy, int *out_left_click)
{
    int32_t dx = (int32_t)mouse_packet[1];
    int32_t dy = (int32_t)mouse_packet[2];
    if (mouse_packet[0] & 0x10) dx |= 0xFFFFFF00;
    if (mouse_packet[0] & 0x20) dy |= 0xFFFFFF00;
    *out_dx = dx; *out_dy = dy;
    int current_left = (mouse_packet[0] & 0x01) ? 1 : 0;
    if (current_left && !mouse_left_prev) *out_left_click = 1;
    mouse_left_prev = current_left;
    mouse_left_down = current_left;
    int current_right = (mouse_packet[0] & 0x02) ? 1 : 0;
    if (current_right && !mouse_right_prev) mouse_right_clicks++;
    mouse_right_prev = current_right;
    mouse_right_down = current_right;
    return 1;
}

int mouse_wheel_take(void) { int w = mouse_wheel_acc; mouse_wheel_acc = 0; return w; }
int mouse_right_take_click(void) { if (mouse_right_clicks > 0) { mouse_right_clicks--; return 1; } return 0; }
int mouse_right_held(void) { return mouse_right_down; }

#if defined(__x86_64__) || defined(__aarch64__)
#ifdef __x86_64__
#define MOUSE_IRQ_SAVE(fl)    __asm__ volatile ("pushfq; cli; popq %0" : "=r"(fl) :: "memory")
#define MOUSE_IRQ_RESTORE(fl) __asm__ volatile ("pushq %0; popfq" :: "r"(fl) : "memory", "cc")
#else                                                                           /* PARCHE 063: el raton USB tambien mueve el cursor en la Pi 5 */
#define MOUSE_IRQ_SAVE(fl)    __asm__ volatile ("mrs %0, daif\n msr daifset, #2" : "=r"(fl) :: "memory")
#define MOUSE_IRQ_RESTORE(fl) __asm__ volatile ("msr daif, %0" :: "r"(fl) : "memory")
#endif
/* PARCHE 047: raton USB (xHCI). Los movimientos se acumulan aqui y mouse_poll los entrega como un paquete mas. dx/dy en convencion PS/2
 * (dy positivo = hacia arriba); wheel positivo = hacia arriba; buttons: bit 0 izquierdo, bit 1 derecho. */
static volatile int32_t inj_dx, inj_dy, inj_wheel; static volatile uint8_t inj_btn, inj_dirty;

void mouse_inject(int32_t dx, int32_t dy, uint8_t buttons, int wheel)
{
    unsigned long fl;
    MOUSE_IRQ_SAVE(fl);
    inj_dx += dx; inj_dy += dy; inj_wheel += wheel; inj_btn = buttons; inj_dirty = 1;
    MOUSE_IRQ_RESTORE(fl);
}

static int mouse_take_injected(int32_t *out_dx, int32_t *out_dy, int *out_left_click)
{
    unsigned long fl;
    MOUSE_IRQ_SAVE(fl);
    int d = inj_dirty; int32_t dx = inj_dx, dy = inj_dy, wh = inj_wheel; uint8_t bt = inj_btn;
    inj_dx = 0; inj_dy = 0; inj_wheel = 0; inj_dirty = 0;
    MOUSE_IRQ_RESTORE(fl);
    if (!d) return 0;
    *out_dx = dx; *out_dy = dy;
    int left = bt & 1, right = (bt >> 1) & 1;
    if (left && !mouse_left_prev) *out_left_click = 1;
    mouse_left_prev = left; mouse_left_down = left;
    if (right && !mouse_right_prev) mouse_right_clicks++;
    mouse_right_prev = right; mouse_right_down = right;
    mouse_wheel_acc += wh;
    return 1;
}
#endif

int mouse_poll(int32_t *out_dx, int32_t *out_dy, int *out_left_click)
{
    *out_dx = 0; *out_dy = 0; *out_left_click = 0;
#if defined(__x86_64__) || defined(__aarch64__)
    if (mouse_take_injected(out_dx, out_dy, out_left_click)) return 1;
#endif
    while (inb(0x64) & 0x01) {
        uint8_t status = inb(0x64);
        if (!(status & 0x20)) break;
        uint8_t b = inb(0x60);

        if (mouse_cycle == 0) {
            if ((b & 0x08) == 0) continue;
            mouse_packet[0] = b; mouse_cycle = 1;
        } else if (mouse_cycle == 1) {
            mouse_packet[1] = b; mouse_cycle = 2;
        } else if (mouse_cycle == 2) {
            mouse_packet[2] = b;
            if (mouse_intelli) { mouse_cycle = 3; continue; }
            mouse_cycle = 0;
            if (mouse_packet[0] & 0xC0) continue;
            return mouse_finish_packet(out_dx, out_dy, out_left_click);
        } else {
            mouse_packet[3] = b; mouse_cycle = 0;
            if (mouse_packet[0] & 0xC0) continue;
            int z = (int8_t)mouse_packet[3];                    /* ID 3: Z con signo (positivo = hacia el usuario) */
            if (z > 7) z = 7; if (z < -7) z = -7;
            mouse_wheel_acc += -z;                              /* acumulado: positivo = rueda hacia arriba */
            return mouse_finish_packet(out_dx, out_dy, out_left_click);
        }
    }
    return 0;
}

int mouse_left_held(void)
{
    return mouse_left_down;
}

int point_in_rect(int32_t px, int32_t py, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh)
{
    return (px >= (int32_t)rx && px < (int32_t)(rx + rw) &&
            py >= (int32_t)ry && py < (int32_t)(ry + rh));
}
