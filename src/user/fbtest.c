/* fbtest <ancho> <alto> <espera_ms> <mantener_ms>: dibuja un patron determinista en pantalla y lo mantiene.
 * Fotograma A (pantalla entera): R = x*255/(w-1), G = y*255/(h-1), B = (x ^ y) & 255.
 * Despues un rectangulo de 200x100 en (100, 50) de color 0x00FF8000. tools/fbcheck.py lo verifica pixel a pixel. */
#include "ulib.h"

#define MAXW 1280
#define MAXH 720
static unsigned int buf[MAXW * MAXH];          /* hasta 1280x720 (3,5 MiB): cabe en los 4 MiB de datos */
static unsigned int rect[200 * 100];

static char line[160];
static int ln;
static void cat(const char *s) { while (*s && ln < (int)sizeof line - 1) line[ln++] = *s++; }
static void catdec(u64 v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } cat(b + i); }
static u64 num(const char **p) { u64 v = 0; while (**p == ' ') (*p)++; while (**p >= '0' && **p <= '9') v = v * 10 + (u64)(*(*p)++ - '0'); return v; }
static void flush(void) { u_write(line, (u64)ln); ln = 0; }

void _start(void)
{
    char a[64];
    if (u_getargs(a, sizeof a) <= 0) { u_puts("[fbtest] uso: exec -b /bin/fbtest.elf <ancho> <alto> <espera_ms> <mantener_ms>\n"); u_exit(2); }
    const char *p = a;
    u64 w = num(&p), h = num(&p), delay = num(&p), hold = num(&p);
    if (w < 320 || w > MAXW || h < 200 || h > MAXH) { u_puts("[fbtest] tamano fuera de rango\n"); u_exit(2); }
    if (delay) u_sleep(delay);

    i64 r = u_fb_open(w, h);
    if (r < 0) { cat("[fbtest] no se pudo abrir la pantalla: -"); catdec((u64)(-r)); cat("\n"); flush(); u_exit(3); }
    unsigned int info[3];
    if (u_fb_info(info) != 0 || info[0] != w || info[1] != h || info[2] != w * 4) { u_puts("[fbtest] fb_info incorrecto\n"); u_fb_close(); u_exit(4); }

    for (u64 y = 0; y < h; ++y)
        for (u64 x = 0; x < w; ++x)
            buf[y * w + x] = (unsigned int)(((x * 255 / (w - 1)) << 16) | ((y * 255 / (h - 1)) << 8) | ((x ^ y) & 255));
    if (u_fb_blit(buf, w, h, 0, 0) != 0) { u_puts("[fbtest] blit fallo\n"); u_fb_close(); u_exit(5); }
    u_sleep(300);
    for (int i = 0; i < 200 * 100; ++i) rect[i] = 0x00FF8000u;
    if (u_fb_blit(rect, 200, 100, 100, 50) != 0) { u_puts("[fbtest] blit del rectangulo fallo\n"); u_fb_close(); u_exit(5); }
    /* rechazos: fuera de pantalla y puntero de kernel */
    int rej = u_fb_blit(rect, 200, 100, w - 100, h - 50) < 0 && u_fb_blit((void *)0x100000, 200, 100, 0, 0) < 0 && u_fb_open(w, h) < 0;

    cat("[fbtest] listo "); catdec(w); cat("x"); catdec(h); cat(rej ? " rechazos=ok\n" : " rechazos=ERROR\n"); flush();
    if (hold) u_sleep(hold);
    u_fb_close();
    u_puts("[fbtest] cerrado\n");
    u_exit(rej ? 0 : 1);
}
