/* ssetest <modo> <N>: coma flotante (SSE2 y x87) en espacio de usuario. Varias copias a la vez se turnan por
 * preemption: si el kernel no guardara el estado SSE/x87 por proceso, los resultados saldrian corruptos.
 *   modo 0: redondeo al mas cercano (MXCSR por defecto); 1: hacia +infinito; 2: hacia cero (cada proceso conserva el suyo)
 * Calcula sum_{k=1..N} 1/(k*k) en doble precision (el host calcula lo mismo en Python: el resultado debe coincidir bit
 * a bit con el modo 0), repite el calculo en x87 con long double y comprueba que MXCSR sigue siendo el que puso. */
#include "ulib.h"

static char line[200];
static int ln;
static void cat(const char *s) { while (*s && ln < (int)sizeof line - 1) line[ln++] = *s++; }
static void catdec(u64 v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } cat(b + i); }
static void cathex(u64 v) { static const char H[] = "0123456789abcdef"; char b[17]; for (int i = 15; i >= 0; --i) { b[i] = H[v & 15]; v >>= 4; } b[16] = 0; cat(b); }

static u64 num(const char *s) { u64 v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (u64)(*s++ - '0'); return v; }

static double series(u64 n)
{
    double s = 0.0;
    for (u64 k = 1; k <= n; ++k) s += 1.0 / ((double)k * (double)k);
    return s;
}

static long double series_x87(u64 n)
{
    long double s = 0.0L;
    for (u64 k = 1; k <= n; ++k) s += 1.0L / ((long double)k * (long double)k);
    return s;
}

void _start(void)
{
    char a[64];
    if (u_getargs(a, sizeof a) <= 0) { u_puts("[ssetest] uso: exec -b /bin/ssetest.elf <modo 0-2> <N>\n"); u_exit(2); }
    const char *p = a; u64 mode = num(p); while (*p && *p != ' ') p++; while (*p == ' ') p++;
    u64 n = num(p);
    if (mode > 2 || n == 0) { u_puts("[ssetest] argumentos invalidos\n"); u_exit(2); }

    unsigned int mx = 0x1F80u | (unsigned int)(mode == 1 ? 2u << 13 : mode == 2 ? 3u << 13 : 0u);
    __asm__ volatile ("ldmxcsr %0" : : "m"(mx));

    double r1 = series(n), r2 = series(n);                       /* dos pasadas: deben ser identicas */
    long double x1 = series_x87(n / 4 + 1), x2 = series_x87(n / 4 + 1);
    unsigned int mx2; __asm__ volatile ("stmxcsr %0" : "=m"(mx2));

    union { double d; u64 u; } c1 = { r1 }, c2 = { r2 };
    int x87_ok = x1 == x2;
    int mx_ok = (mx2 & 0xFFC0u) == (mx & 0xFFC0u);               /* modo de redondeo y mascaras intactos (los bits 0-5 son indicadores que cambian solos) */
    cat("[ssetest] modo="); catdec(mode); cat(" N="); catdec(n); cat(" sum="); cathex(c1.u);
    cat(c1.u == c2.u ? " dos_pasadas=ok" : " dos_pasadas=ERROR"); cat(x87_ok ? " x87=ok" : " x87=ERROR"); cat(mx_ok ? " mxcsr=ok\n" : " mxcsr=ERROR\n");
    u_write(line, (u64)ln);
    u_exit(c1.u == c2.u && x87_ok && mx_ok ? 0 : 1);
}
