#include "rpi_fb.h"
#include "platform.h"
#include "mbox_proto.h"
#include "console.h"
#include "idt.h"

/* PARCHE 063. Mailbox del VideoCore: el mismo diseno que en las Pi anteriores (lectura +0, estado +0x18, escritura +0x20). */
#define MBOX_FULL   0x80000000u
#define MBOX_EMPTY  0x40000000u
#define MBOX_CH_PROP 8u

static inline uint32_t cache_line(void)
{
    uint64_t ctr; __asm__ volatile ("mrs %0, ctr_el0" : "=r"(ctr));
    return 4u << ((ctr >> 16) & 0xF);                     /* DminLine: palabras de 4 bytes */
}

void soma_dcache_clean(const volatile void *p, size_t n)
{
    uintptr_t line = cache_line(), a = (uintptr_t)p & ~(line - 1), e = (uintptr_t)p + n;
    for (; a < e; a += line) __asm__ volatile ("dc cvac, %0" :: "r"(a) : "memory");
    __asm__ volatile ("dsb sy" ::: "memory");
}

void soma_dcache_flush_inval(const volatile void *p, size_t n)
{
    uintptr_t line = cache_line(), a = (uintptr_t)p & ~(line - 1), e = (uintptr_t)p + n;
    for (; a < e; a += line) __asm__ volatile ("dc civac, %0" :: "r"(a) : "memory");
    __asm__ volatile ("dsb sy" ::: "memory");
}

/* Envia el buffer (alineado a 16 bytes, por debajo de 4 GiB) y espera la respuesta. 0 = bien. */
static int mbox_call(uint32_t *buf, uint32_t bytes)
{
    if (!SOMA_MBOX_BASE) return -1;
    volatile uint32_t *rd = (volatile uint32_t *)(uintptr_t)SOMA_MBOX_BASE;
    volatile uint32_t *st = (volatile uint32_t *)(uintptr_t)(SOMA_MBOX_BASE + 0x18);
    volatile uint32_t *wr = (volatile uint32_t *)(uintptr_t)(SOMA_MBOX_BASE + 0x20);
    uint32_t msg = (uint32_t)(uintptr_t)buf | MBOX_CH_PROP;
    soma_dcache_flush_inval(buf, bytes);
    uint64_t t0 = timer_ticks;
    unsigned long guard = 20000000UL;                     /* PARCHE 075: tambien funciona antes de que el temporizador cuente */
    while (*st & MBOX_FULL) if (timer_ticks - t0 > 100 || !--guard) return -2;
    __asm__ volatile ("dsb sy" ::: "memory");
    *wr = msg;
    for (;;) {
        while (*st & MBOX_EMPTY) if (timer_ticks - t0 > 500 || !--guard) return -3;
        if (*rd == msg) break;                            /* otra respuesta de otro canal: se descarta */
    }
    soma_dcache_flush_inval(buf, bytes);                  /* el firmware escribio en RAM */
    return 0;
}

/* PARCHE 079: acceso publico al canal de propiedades (revision de la placa, etc.) */
int rpi_mbox_prop(uint32_t *buf, uint32_t bytes) { return mbox_call(buf, bytes); }

/* PARCHE 075 (primera prueba en una Pi 5 real: pantalla negra). Dos causas corregidas aqui:
 *  1. SOMA no dibujaba nada en HDMI hasta ejecutar 'vsoma': ahora hay una consola de texto en HDMI desde el principio del arranque.
 *  2. El firmware de la Pi 5 NO deja elegir la resolucion (impone la del monitor) y no atiende todas las etiquetas: se acepta lo que de. */
static int g_last_err;
static int g_swap_rb;                                      /* PARCHE 076: 'hdmi swap' intercambia rojo y azul si en placa salen cambiados */
static struct mbox_fb_result g_res;
static uint32_t g_raw_base;

static struct mbox_fb_result g_raw;                        /* lo que declaro el firmware, sin corregir (comando 'hdmi') */
static uint32_t g_phys_w, g_phys_h;
static int g_fixed;

#ifdef SOMA_FB_TEST16
/* Prueba en el emulador: imita el firmware de la Pi 5 real. La pantalla "real" es siempre 1280x720 a 16 bits; la respuesta devuelve el tamano pedido y 32 bits. */
static void sim_before(uint32_t *buf, uint32_t n)
{
    for (uint32_t q = 2; q + 4 < n; ) {
        uint32_t id = buf[q], len = buf[q + 1] / 4;
        if (!id) break;
        if (id == 0x00048003u || id == 0x00048004u) { buf[q + 3] = 1280; buf[q + 4] = 720; }
        if (id == 0x00048005u) buf[q + 3] = 16;
        if (id == 0x00048006u) buf[q + 3] = 1;
        q += 3 + len;
    }
}
static void sim_after(uint32_t *buf, uint32_t n, uint32_t w, uint32_t h)
{
    for (uint32_t q = 2; q + 4 < n; ) {
        uint32_t id = buf[q], len = buf[q + 1] / 4;
        if (!id) break;
        if (id == 0x00048003u || id == 0x00048004u) { buf[q + 3] = w; buf[q + 4] = h; }
        if (id == 0x00048005u || id == 0x00040005u) buf[q + 3] = 32;
        q += 3 + len;
    }
}
#else
#define sim_before(b, n) ((void)0)
#define sim_after(b, n, w, h) ((void)0)
#endif

/* PARCHE 078: la geometria real se saca de la pitch y del tamano reservado; ancho, alto y profundidad declarados solo valen si cuadran con ellos. */
static int fb_finish(struct mbox_fb_result *res)
{
    static uint32_t pb[8] __attribute__((aligned(16)));
    g_raw = *res; g_phys_w = g_phys_h = 0;
    uint32_t n = mbox_fb_build_getphys(pb, 8);
    if (n && mbox_call(pb, n * 4) == 0) (void)mbox_fb_parse_phys(pb, n, &g_phys_w, &g_phys_h);
    g_fixed = mbox_fb_fix(res, g_phys_w, g_phys_h);
    return 0;
}

static int fb_request(uint32_t w, uint32_t h, struct mbox_fb_result *res)
{
    static uint32_t buf[MBOX_FB_WORDS] __attribute__((aligned(16)));
    static const uint32_t fall[][2] = { { 1280, 720 }, { 1024, 768 }, { 800, 600 } };
    uint32_t tw = w, th = h;
    int r = -5;
    for (int k = 0; k < 4; k++) {                          /* 1) tamano exacto pedido, bajando si no cabe: solo vale si la respuesta es coherente (emulador) */
        if (k) {
            int pick = -1;
            for (int j = 0; j < 3; j++) if ((uint64_t)fall[j][0] * fall[j][1] < (uint64_t)tw * th) { pick = j; break; }
            if (pick < 0) break;
            tw = fall[pick][0]; th = fall[pick][1];
        }
        uint32_t n = mbox_fb_build(buf, MBOX_FB_WORDS, tw, th, 0);
        if (!n) { r = -4; break; }
        sim_before(buf, n);
        r = mbox_call(buf, n * 4);
        if (r != 0) return r;                              /* el mailbox no responde: no tiene sentido insistir */
        sim_after(buf, n, tw, th);
        if (mbox_fb_parse(buf, n, res) == 0 && res->width == tw && res->height == th && mbox_fb_consistent(res)) { g_raw = *res; g_fixed = 0; return 0; }
        if (mbox_fb_parse_any(buf, n, res) == 0 && !mbox_fb_consistent(res)) break;   /* Pi 5 real: eco del tamano pedido; pedir otros tamanos no cambia nada */
        r = -5;
    }
    {                                                      /* 2) peticion completa: se acepta el buffer y se corrige la geometria con pitch y tamano */
        uint32_t n = mbox_fb_build(buf, MBOX_FB_WORDS, w, h, 0);
        sim_before(buf, n);
        if (n && mbox_call(buf, n * 4) == 0) {
            sim_after(buf, n, w, h);
            if (mbox_fb_parse_any(buf, n, res) == 0) return fb_finish(res);
        }
    }
    {                                                      /* 3) peticion minima: solo leer el modo actual y reservar el buffer */
        uint32_t n = mbox_fb_build_query(buf, MBOX_FB_WORDS);
        if (n && mbox_call(buf, n * 4) == 0) {
            sim_after(buf, n, w, h);
            if (mbox_fb_parse_any(buf, n, res) == 0) return fb_finish(res);
        }
    }
    return r;
}

/* ---- consola de texto en HDMI ---- */
#include "../../fonts/font_classic.h"
static volatile uint8_t *con_base;
static uint32_t con_w, con_h, con_pitch, con_bpp, con_s, con_cols, con_rows, con_x, con_y;
static int con_on, con_tried, con_esc, con_bold;
static uint32_t con_arg[4], con_narg, con_fg = 0xC0C0C0u;

static inline void con_px(uint32_t x, uint32_t y, uint32_t rgb)
{
    if (g_swap_rb) rgb = (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu) | ((rgb & 0xFFu) << 16);
    volatile uint8_t *p = con_base + (uint64_t)y * con_pitch + (uint64_t)x * con_bpp;
    if (con_bpp == 4) *(volatile uint32_t *)p = rgb;
    else if (con_bpp == 2) *(volatile uint16_t *)p = (uint16_t)(((rgb >> 8) & 0xF800u) | ((rgb >> 5) & 0x07E0u) | ((rgb >> 3) & 0x1Fu));
    else { p[0] = (uint8_t)rgb; p[1] = (uint8_t)(rgb >> 8); p[2] = (uint8_t)(rgb >> 16); }
}

static void con_fill_rows(uint32_t y0, uint32_t y1)       /* pone a negro las filas de pixeles [y0, y1) */
{
    for (uint32_t y = y0; y < y1 && y < con_h; y++) {
        volatile uint32_t *p = (volatile uint32_t *)(con_base + (uint64_t)y * con_pitch);
        for (uint32_t i = 0; i < con_pitch / 4; i++) p[i] = 0;
    }
    if (y1 > con_h) y1 = con_h;
    if (y1 > y0) soma_dcache_clean(con_base + (uint64_t)y0 * con_pitch, (size_t)(y1 - y0) * con_pitch);
}

static void con_scroll(void)                               /* sube 8 lineas de texto de una vez (copiar toda la pantalla por linea seria lento) */
{
    uint32_t ch = 8 * con_s, lines = con_rows > 16 ? 8 : 1, dy = lines * ch, keep = (con_rows - lines) * ch;
    for (uint32_t y = 0; y < keep; y++) {
        volatile uint32_t *d = (volatile uint32_t *)(con_base + (uint64_t)y * con_pitch);
        volatile uint32_t *q = (volatile uint32_t *)(con_base + (uint64_t)(y + dy) * con_pitch);
        for (uint32_t i = 0; i < con_pitch / 4; i++) d[i] = q[i];
    }
    soma_dcache_clean(con_base, (size_t)keep * con_pitch);
    con_fill_rows(keep, con_rows * ch);
    con_y = con_rows - lines;
}

static void con_glyph(char c)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *g = font_glyph_classic[c - 32];
    uint32_t px = con_x * 8 * con_s, py = con_y * 8 * con_s;
    for (uint32_t r = 0; r < 8; r++)
        for (uint32_t sy = 0; sy < con_s; sy++) {
            uint32_t y = py + r * con_s + sy;
            for (uint32_t b = 0; b < 8; b++) {
                uint32_t col = (g[r] & (0x80u >> b)) ? con_fg : 0;
                for (uint32_t sx = 0; sx < con_s; sx++) con_px(px + b * con_s + sx, y, col);
            }
            soma_dcache_clean(con_base + (uint64_t)y * con_pitch + (uint64_t)px * con_bpp, 8 * con_s * con_bpp);
        }
}

static void con_sgr(void)
{
    static const uint32_t pal[8] = { 0x000000u, 0xCC3333u, 0x33CC33u, 0xCCCC33u, 0x4477EEu, 0xCC33CCu, 0x33CCCCu, 0xC0C0C0u };
    static const uint32_t bri[8] = { 0x666666u, 0xFF5555u, 0x55FF55u, 0xFFFF55u, 0x7799FFu, 0xFF55FFu, 0x55FFFFu, 0xFFFFFFu };
    if (con_narg == 0) { con_fg = 0xC0C0C0u; con_bold = 0; return; }
    for (uint32_t i = 0; i < con_narg && i < 4; i++) {
        uint32_t v = con_arg[i];
        if (v == 0) { con_fg = 0xC0C0C0u; con_bold = 0; }
        else if (v == 1) con_bold = 1;
        else if (v >= 30 && v <= 37) con_fg = con_bold ? bri[v - 30] : pal[v - 30];
        else if (v >= 90 && v <= 97) con_fg = bri[v - 90];
        else if (v == 39) con_fg = 0xC0C0C0u;
    }
}

void rpi_fb_clear(void)
{
    if (!con_on) return;
    con_fill_rows(0, con_h);
    con_x = con_y = 0;
}

void rpi_fb_putc(char c)
{
    if (!con_on) return;
    unsigned char u = (unsigned char)c;
    if (con_esc == 1) { if (c == '[') { con_esc = 2; con_narg = 0; con_arg[0] = con_arg[1] = con_arg[2] = con_arg[3] = 0; } else con_esc = 0; return; }
    if (con_esc == 2) {
        if (c >= '0' && c <= '9') { if (con_narg == 0) con_narg = 1; if (con_narg <= 4) con_arg[con_narg - 1] = con_arg[con_narg - 1] * 10 + (uint32_t)(c - '0'); return; }
        if (c == ';') { if (con_narg == 0) con_narg = 1; if (con_narg < 5) con_narg++; return; }
        if (c == '?') return;
        con_esc = 0;
        if (con_narg > 4) con_narg = 4;
        if (c == 'm') con_sgr();
        else if (c == 'J') rpi_fb_clear();
        else if (c == 'H') { con_x = con_y = 0; }
        return;
    }
    if (u == 27) { con_esc = 1; return; }
    if (c == '\r') { con_x = 0; return; }
    if (c == '\n') { con_x = 0; con_y++; }
    else if (c == '\b') { if (con_x) { con_x--; con_glyph(' '); } return; }
    else if (c == '\t') { con_x = (con_x + 8) & ~7u; }
    else if (u >= 0x80 && u < 0xC0) return;                /* bytes de continuacion UTF-8: el caracter ya se pinto como '?' */
    else if (u >= 32) { con_glyph(c); con_x++; }
    else return;
    if (con_x >= con_cols) { con_x = 0; con_y++; }
    if (con_y >= con_rows) con_scroll();
}

static void dec_out(uint32_t v) { char t[11]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) { char o[2] = { t[--n], 0 }; kprint(o); } }
int rpi_fb_console_active(void) { return con_on; }
int rpi_fb_swap_rb(void) { return g_swap_rb; }
void rpi_fb_toggle_swap(void) { g_swap_rb = !g_swap_rb; }
void rpi_fb_info(void)                                     /* PARCHE 076/078: comando 'hdmi' */
{
    kprint("\033[1;37m");                                   /* blanco puro: se lee igual sea cual sea el formato de pixel */
    kprint("hdmi: en uso "); dec_out(g_res.width); kprint("x"); dec_out(g_res.height); kprint(" "); dec_out(g_res.depth); kprint(" bits, pitch "); dec_out(g_res.pitch);
    kprint(con_on ? ", consola activa" : ", consola inactiva"); kprint(g_swap_rb ? ", R/B cambiados\n" : "\n");
    kprint("hdmi: firmware declara "); dec_out(g_raw.width); kprint("x"); dec_out(g_raw.height); kprint(" "); dec_out(g_raw.depth); kprint(" bits, pitch "); dec_out(g_raw.pitch);
    kprint(", tamano "); dec_out(g_raw.size); kprint(", base 0x"); kprint_hex32(g_raw.base); kprint(", fisico "); dec_out(g_phys_w); kprint("x"); dec_out(g_phys_h);
    kprint(g_fixed ? " -> corregido" : " -> coherente"); kprint(", error "); dec_out((uint32_t)-g_last_err); kprint("\033[0m\n");
}
int rpi_fb_last_error(void) { return g_last_err; }


/* Consola de texto en HDMI. Se puede llamar antes de que funcione el temporizador. Devuelve 1 si la consola esta activa. */
int rpi_fb_init(void)
{
    if (con_tried) return con_on;
    con_tried = 1;
    if (!SOMA_MBOX_BASE) return 0;
    struct mbox_fb_result res;
    int r = fb_request(1280, 720, &res);
    g_last_err = r;
    if (r != 0) { kprint("HDMI: el firmware no ha dado framebuffer (codigo -"); dec_out((uint32_t)-r); kprint(")\n"); return 0; }
    g_raw_base = res.base; g_res = res;
    con_base = (volatile uint8_t *)(uintptr_t)(res.base & 0x3FFFFFFFu);
    con_w = res.width; con_h = res.height; con_pitch = res.pitch; con_bpp = res.depth / 8;
    con_s = con_w >= 1280 ? 2 : 1;
    con_cols = con_w / (8 * con_s); con_rows = con_h / (8 * con_s);
    if (res.size && con_pitch && res.size / con_pitch < con_h) con_rows = (res.size / con_pitch) / (8 * con_s);   /* PARCHE 077: nunca escribir fuera del buffer */
    if (!con_cols || !con_rows || (con_pitch & 3)) { g_last_err = -6; return 0; }
    con_on = 1;
    rpi_fb_clear();
    rpi_fb_info();
    return 1;
}

int rpi_fb_set_mode(uint32_t w, uint32_t h, volatile uint32_t **vram, uint32_t *ow, uint32_t *oh, uint32_t *obpp, uint32_t *opitch)
{
    struct mbox_fb_result res;
    int was = con_on;
    con_on = 0;                                            /* el escritorio toma la pantalla: la consola de texto deja de pintar */
    int r = fb_request(w, h, &res);
    if (r == 0 && (res.depth != 16 && res.depth != 24 && res.depth != 32)) r = -6;
    if (r != 0) { con_on = was; return r; }
    *vram = (volatile uint32_t *)(uintptr_t)(res.base & 0x3FFFFFFFu);        /* direccion de bus del VideoCore -> fisica de la CPU */
    g_res = res;
    *ow = res.width; *oh = res.height; *obpp = res.depth; *opitch = res.pitch;   /* PARCHE 076: fb.c convierte si no son 32 bits */
    return 0;
}
