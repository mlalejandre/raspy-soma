/* Pruebas en host de la validacion de rangos de usuario (src/arch/x86_64/uaccess.h) con casos limite y fuzz. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "uaccess.h"

#define BASE 0x8000000000ULL
#define WIN  0x2000000ULL
#define NPG  (WIN / 4096)

static uint64_t g_pte[NPG];               /* tabla simulada de la ventana */
static uint64_t pte_of(void *ctx, uint64_t va)
{
    (void)ctx;
    if (va < BASE || va >= BASE + WIN) { printf("  FALLO: se consulto una pagina fuera de la ventana 0x%llx\n", (unsigned long long)va); soma_t_fails++; return 0; }
    return g_pte[(va - BASE) / 4096];
}
static int ok(uint64_t p, uint64_t n, int w) { return uaccess_range_ok(p, n, w, BASE, WIN, pte_of, 0); }
static void mapea(uint64_t pg0, uint64_t n, uint64_t fl) { for (uint64_t i = 0; i < n; ++i) g_pte[pg0 + i] = fl; }
#define RW (UACC_PRESENT | UACC_USER | UACC_WRITABLE)
#define RO (UACC_PRESENT | UACC_USER)

static void test_basico(void)
{
    memset(g_pte, 0, sizeof g_pte); mapea(0, 4, RW); mapea(4, 1, RO);
    CHECK(ok(BASE, 1, 0)); CHECK(ok(BASE, 4096 * 4, 1)); CHECK(ok(BASE + 4095, 2, 1));
    CHECK(!ok(BASE, 0, 0));                         /* longitud 0 */
    CHECK(ok(BASE + 4 * 4096, 10, 0));              /* solo lectura: leer si */
    CHECK(!ok(BASE + 4 * 4096, 10, 1));             /* ... escribir no */
    CHECK(!ok(BASE, 4096 * 5 + 1, 0));              /* cruza a pagina sin mapear */
    CHECK(!ok(BASE + 5 * 4096, 1, 0));              /* sin mapear */
    g_pte[1] = UACC_PRESENT;                         /* presente pero sin bit U (pagina de kernel) */
    CHECK(!ok(BASE, 4096 * 2, 0)); CHECK(!ok(BASE + 4096, 1, 0));
}
static void test_limites(void)
{
    memset(g_pte, 0, sizeof g_pte); mapea(0, NPG, RW);
    CHECK(ok(BASE, WIN, 1));                         /* ventana entera */
    CHECK(!ok(BASE, WIN + 1, 0));
    CHECK(ok(BASE + WIN - 1, 1, 1));                 /* ultimo byte */
    CHECK(!ok(BASE + WIN, 1, 0));                    /* justo fuera */
    CHECK(!ok(BASE - 1, 1, 0));                      /* justo antes */
    CHECK(!ok(BASE + WIN - 1, 2, 0));
    CHECK(!ok(0, 1, 0)); CHECK(!ok(0xFFFFFFFF80000000ULL, 16, 0));   /* direcciones de kernel */
    /* desbordamientos: p+len da la vuelta */
    CHECK(!ok(UINT64_MAX, 2, 0)); CHECK(!ok(UINT64_MAX - 7, 16, 1));
    CHECK(!ok(BASE + 8, UINT64_MAX, 0)); CHECK(!ok(BASE, UINT64_MAX - BASE + 2, 0));
    CHECK(!ok(UINT64_MAX - BASE + 2, BASE + 5, 0));
    CHECK(!ok(BASE + WIN - 16, UINT64_MAX - 7, 0));
}
static void test_fuzz(void)
{
    uint64_t s = 0x9E3779B97F4A7C15ULL;
#define RND() (s ^= s << 13, s ^= s >> 7, s ^= s << 17, s)
    int aceptados = 0;
    for (int it = 0; it < 400000; ++it) {
        for (int i = 0; i < 40; ++i) g_pte[i] = (it & 1) ? (RND() % 4 == 0 ? 0 : (RND() & 7)) : (RND() % 16 == 0 ? RO : RW);
        for (uint64_t i = 40; i < NPG; ++i) g_pte[i] = 0;
        uint64_t p, n;
        switch (RND() % 5) {
        case 0: p = RND(); n = RND(); break;
        case 1: p = BASE + RND() % (48 * 4096); n = RND() % (50 * 4096); break;
        case 2: p = BASE + WIN - RND() % 64; n = RND() % 128; break;
        case 3: p = UINT64_MAX - RND() % 64; n = RND() % 128; break;
        default: p = BASE + RND() % WIN; n = (RND() % 2) ? RND() : RND() % 4096; break;
        }
        int w = (int)(RND() & 1);
        int r = ok(p, n, w);
        /* referencia lenta: en 128 bits no hay desbordamiento */
        __uint128_t e = (__uint128_t)p + n;
        int ref = n != 0 && p >= BASE && e <= (__uint128_t)BASE + WIN;
        if (ref) for (uint64_t a = p & ~0xFFFULL; a < (uint64_t)e; a += 4096) {
            uint64_t f = g_pte[(a - BASE) / 4096];
            if (!(f & UACC_PRESENT) || !(f & UACC_USER) || (w && !(f & UACC_WRITABLE))) { ref = 0; break; }
        }
        CHECK_EQ_INT(r, ref);
        aceptados += r;
        if (soma_t_fails > 20) break;
    }
    CHECK(aceptados > 1000);                         /* el fuzz tambien ejercita la rama de exito */
}
int main(void)
{
    test_basico(); test_limites(); test_fuzz();
    TEST_MAIN_END("uaccess");
}
