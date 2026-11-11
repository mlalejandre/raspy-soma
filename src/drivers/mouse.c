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

static const struct soma_cursor_entry CURSOR_REGISTRY[CURSOR_COUNT] = {
    [CURSOR_ARROW]     = { "arrow",     "Flecha clasica de precision",   CURSOR_ARROW_W, CURSOR_ARROW_H, (const uint8_t *)cursor_arrow_data },
    [CURSOR_CROSSHAIR] = { "crosshair", "Cruz de seleccion y diseno",   CURSOR_CROSS_W, CURSOR_CROSS_H, (const uint8_t *)cursor_cross_data },
    [CURSOR_HAND]      = { "hand",      "Mano de navegacion y enlaces",  CURSOR_HAND_W,  CURSOR_HAND_H,  (const uint8_t *)cursor_hand_data }
};

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
    const struct soma_cursor_entry *c = &CURSOR_REGISTRY[active_cursor_id];
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
