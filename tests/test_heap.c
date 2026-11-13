/* Pruebas en host del asignador real (src/core/mem.c) sobre una arena estatica.
 * Se compila con -include tests/host_heap_cfg.h (ver Makefile). */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "mem.h"

static struct kheap_counters ctr(void)
{
    struct kheap_counters c;
    kheap_get_counters(&c);
    return c;
}

static int in_arena(const void *p, size_t n)
{
    const uint8_t *a = (const uint8_t *)p;
    return a >= host_heap_arena && a + n <= host_heap_arena + HOST_ARENA_SIZE;
}

static void test_basico(void)
{
    struct kheap_counters c0 = ctr(), c1;
    uint8_t *a = (uint8_t *)kmalloc(1);
    uint8_t *b = (uint8_t *)kmalloc(100);
    uint8_t *c = (uint8_t *)kmalloc(4096);

    CHECK(a && b && c);
    CHECK_EQ_INT((uintptr_t)a % 16, 0);
    CHECK_EQ_INT((uintptr_t)b % 16, 0);
    CHECK_EQ_INT((uintptr_t)c % 16, 0);
    CHECK(in_arena(a, 1) && in_arena(b, 100) && in_arena(c, 4096));
    CHECK(a + 1 <= b || b + 100 <= a);
    CHECK(b + 100 <= c || c + 4096 <= b);
    memset(a, 0x11, 1); memset(b, 0x22, 100); memset(c, 0x33, 4096);
    c1 = ctr();
    CHECK_EQ_INT(c1.live_blocks, c0.live_blocks + 3);
    CHECK_EQ_INT(c1.alloc_calls, c0.alloc_calls + 3);
    CHECK(c1.used_bytes > c0.used_bytes);
    CHECK(c1.peak_used_bytes >= c1.used_bytes);
    kfree(a); kfree(b); kfree(c);
    c1 = ctr();
    CHECK_EQ_INT(c1.live_blocks, c0.live_blocks);
    CHECK_EQ_INT(c1.used_bytes, c0.used_bytes);
    CHECK_EQ_INT(kheap_check(), 0);
}

/* Antes del parche 005, kmalloc((size_t)-1) redondeaba a 0 y devolvia un bloque minimo. */
static void test_tamanos_imposibles(void)
{
    struct kheap_counters c0 = ctr(), c1;

    CHECK(kmalloc(0) == 0);
    CHECK(kmalloc((size_t)-1) == 0);
    CHECK(kmalloc((size_t)-8) == 0);
    CHECK(kmalloc((size_t)-16) == 0);
    CHECK(kmalloc(KHEAP_MAX_SIZE + 1) == 0);
    CHECK(kmalloc(KHEAP_MAX_SIZE) == 0);          /* no cabe: cabecera + cola + paginas */
    c1 = ctr();
    CHECK_EQ_INT(c1.failed_allocs, c0.failed_allocs + 5);
    CHECK_EQ_INT(c1.corruptions, c0.corruptions);
    CHECK_EQ_INT(c1.mapped_bytes, c0.mapped_bytes);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_coalescencia(void)
{
    struct kheap_counters c0 = ctr(), c1;
    void *a = kmalloc(200), *b = kmalloc(200), *c = kmalloc(200), *big;

    CHECK(a && b && c);
    kfree(b); kfree(a); kfree(c);
    c1 = ctr();
    CHECK_EQ_INT(c1.used_bytes, c0.used_bytes);
    CHECK_EQ_INT(kheap_check(), 0);
    /* Todo fusionado: cabe un bloque mayor que cualquiera de los tres sin crecer. */
    big = kmalloc(600);
    CHECK(big != 0);
    CHECK_EQ_INT(ctr().mapped_bytes, c0.mapped_bytes);
    kfree(big);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_veneno_al_liberar(void)
{
    uint8_t *g1 = (uint8_t *)kmalloc(16);
    uint8_t *p = (uint8_t *)kmalloc(64);
    uint8_t *g2 = (uint8_t *)kmalloc(16);
    int i, ok = 1;

    memset(p, 0x42, 64);
    kfree(p);
    for (i = 0; i < 48; ++i) if (p[i] != 0xDD) ok = 0;   /* (los ultimos 24 bytes de un bloque libre llevan su nodo de listas) */
    CHECK(ok);                                       /* uso tras liberar se nota */
    kfree(g1); kfree(g2);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_liberaciones_invalidas(void)
{
    uint8_t *g1 = (uint8_t *)kmalloc(16);
    uint8_t *p = (uint8_t *)kmalloc(48);
    uint8_t *g2 = (uint8_t *)kmalloc(16);
    struct kheap_counters c0 = ctr(), c1;

    kfree(p);                                        /* vecinos ocupados: no se fusiona */
    host_out_reset();
    kfree(p);                                        /* segunda vez */
    CHECK(strstr(host_out(), "double free") != 0);
    CHECK_EQ_INT(ctr().rejected_frees, c0.rejected_frees + 1);

    host_out_reset();
    kfree(g1 + 8);                                   /* no alineado */
    kfree(g1 + 16);                                  /* interior alineado */
    kfree((void *)(KHEAP_START + 16));               /* antes de la primera cabecera */
    kfree((void *)(KHEAP_START + KHEAP_MAX_SIZE + 4096)); /* fuera del heap */
    kfree(0);                                        /* NULL es legal y no cuenta */
    CHECK(strstr(host_out(), "rechazado") != 0);
    c1 = ctr();
    CHECK_EQ_INT(c1.rejected_frees, c0.rejected_frees + 5);
    CHECK_EQ_INT(c1.corruptions, c0.corruptions);

    kfree(g1); kfree(g2);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_desbordamiento_de_un_byte(void)
{
    uint8_t *p = (uint8_t *)kmalloc(24);             /* bloque de 32: cola en [24,32) */
    struct kheap_counters c0 = ctr();

    CHECK(p != 0);
    memset(p, 0x77, 24);
    CHECK_EQ_INT(kheap_check(), 0);
    p[24] = 0x00;                                    /* un solo byte de mas */
    host_out_reset();
    CHECK(kheap_check() >= 1);
    CHECK(strstr(host_out(), "desbordamiento") != 0);
    host_out_reset();
    kfree(p);
    CHECK(strstr(host_out(), "desbordamiento") != 0);
    CHECK(ctr().corruptions >= c0.corruptions + 2);
    CHECK_EQ_INT(kheap_check(), 0);                  /* el bloque se libero y el heap sigue sano */
    p = (uint8_t *)kmalloc(24);
    CHECK(p != 0);
    kfree(p);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_cabecera_danada(void)
{
    uint8_t *q = (uint8_t *)kmalloc(32);
    uint8_t saved[32];
    struct kheap_counters c0 = ctr();

    CHECK(q != 0);
    memcpy(saved, q - 32, 32);
    ((uint32_t *)(void *)(q - 32))[3] = 0;           /* magic = 0 */
    host_out_reset();
    CHECK(kheap_check() >= 1);
    kfree(q);                                        /* no debe liberar un bloque danado */
    CHECK(strstr(host_out(), "cabecera") != 0);
    CHECK(ctr().rejected_frees > c0.rejected_frees);
    memcpy(q - 32, saved, 32);                       /* reparar y liberar de verdad */
    kfree(q);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_cabecera_vecina_pisada(void)
{
    uint8_t *a = (uint8_t *)kmalloc(16);
    uint8_t *b = (uint8_t *)kmalloc(16);
    uint8_t saved[96];

    CHECK(a && b);
    if (!a || !b || b != a + 64) { kfree(a); kfree(b); CHECK(!"bloques no adyacentes"); return; }
    memcpy(saved, a, sizeof(saved));
    memset(a, 0x00, 64);                             /* desborda a y destruye la cabecera de b */
    host_out_reset();
    CHECK(kheap_check() >= 1);
    /* PARCHE 030: kmalloc ya no recorre bloques ocupados (listas de libres), asi que puede seguir sirviendo desde
     * un bloque libre sano; lo que no debe es colgarse ni tocar la cabecera danada. kheap_check la detecta. */
    { void *z = kmalloc(16); if (z) kfree(z); }
    CHECK(strstr(host_out(), "CORRUPCION") != 0);
    memcpy(a, saved, sizeof(saved));                 /* reparar */
    CHECK_EQ_INT(kheap_check(), 0);
    kfree(b); kfree(a);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_sin_memoria_inyectado(void)
{
    struct kheap_counters c0 = ctr();
    void *p;

    host_pmm_fail_after(0);                          /* el PMM no da ni un marco */
    p = kmalloc(1u << 20);
    CHECK(p == 0);
    CHECK_EQ_INT(ctr().mapped_bytes, c0.mapped_bytes);
    CHECK_EQ_INT(ctr().failed_allocs, c0.failed_allocs + 1);
    CHECK_EQ_INT(kheap_check(), 0);

    host_pmm_fail_after(3);                          /* falla a mitad de la ampliacion */
    p = kmalloc(1u << 20);
    CHECK(p == 0);
    CHECK_EQ_INT(ctr().mapped_bytes, c0.mapped_bytes);
    CHECK_EQ_INT(host_pmm_frames_live(), (int)(c0.mapped_bytes / 4096));  /* sin marcos perdidos */
    CHECK_EQ_INT(kheap_check(), 0);

    host_pmm_fail_after(-1);
    p = kmalloc(1u << 20);
    CHECK(p != 0);
    CHECK(ctr().mapped_bytes > c0.mapped_bytes);
    CHECK_EQ_INT(host_pmm_frames_live(), (int)(ctr().mapped_bytes / 4096));
    kfree(p);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_tope_de_arena(void)
{
    struct kheap_counters c0 = ctr(), c1;
    void *blk[64];
    int n = 0, i;

    while (n < 64 && (blk[n] = kmalloc(256 * 1024)) != 0) n++;
    CHECK(n >= 1 && n < 64);                         /* se agota al llegar al tope */
    c1 = ctr();
    CHECK(c1.mapped_bytes <= KHEAP_MAX_SIZE);
    CHECK(c1.failed_allocs > c0.failed_allocs);
    CHECK_EQ_INT(kheap_check(), 0);
    for (i = 0; i < n; ++i) kfree(blk[i]);
    CHECK_EQ_INT(ctr().used_bytes, c0.used_bytes);
    CHECK_EQ_INT(kheap_check(), 0);
    CHECK_EQ_INT(host_pmm_frames_live(), (int)(ctr().mapped_bytes / 4096));
}

typedef struct { uint8_t *p; uint32_t n; uint8_t tag; } slot_t;

static int slot_ok(const slot_t *s)
{
    uint32_t i;
    for (i = 0; i < s->n; ++i)
        if (s->p[i] != (uint8_t)(s->tag + i)) return 0;
    return 1;
}

/* Mezcla determinista de asignaciones y liberaciones con verificacion de contenido. */
static void test_estres(void)
{
    enum { SLOTS = 300, OPS = 30000 };
    static slot_t s[SLOTS];
    struct kheap_counters c0 = ctr();
    uint32_t seed = 0x5EEDu;
    int op, corrupt_data = 0, bad_check = 0, k;

    memset(s, 0, sizeof(s));
    for (op = 0; op < OPS; ++op) {
        uint32_t i;
        seed = seed * 1664525u + 1013904223u;
        k = (int)((seed >> 8) % SLOTS);
        if (!s[k].p) {
            uint32_t n = 1 + (seed >> 12) % 3000;
            if ((seed & 0x3F) == 0) n = 4096 + (seed >> 14) % 16000;   /* algunos grandes */
            s[k].p = (uint8_t *)kmalloc(n);
            if (!s[k].p) continue;
            s[k].n = n; s[k].tag = (uint8_t)(seed >> 4);
            for (i = 0; i < n; ++i) s[k].p[i] = (uint8_t)(s[k].tag + i);
        } else {
            if (!slot_ok(&s[k])) corrupt_data++;
            kfree(s[k].p);
            s[k].p = 0;
        }
        if (op % 1000 == 0 && kheap_check() != 0) bad_check++;
    }
    CHECK_EQ_INT(corrupt_data, 0);
    CHECK_EQ_INT(bad_check, 0);
    for (k = 0; k < SLOTS; ++k) {
        if (s[k].p) { if (!slot_ok(&s[k])) corrupt_data++; kfree(s[k].p); }
    }
    CHECK_EQ_INT(corrupt_data, 0);
    CHECK_EQ_INT(ctr().live_blocks, c0.live_blocks);
    CHECK_EQ_INT(ctr().used_bytes, c0.used_bytes);   /* sin fugas */
    CHECK_EQ_INT(kheap_check(), 0);
    CHECK_EQ_INT(ctr().corruptions, c0.corruptions);
}

/* PARCHE 030: mas bloques vivos, comprobacion del heap cada 100 operaciones y coalescencia total al final
 * (con las listas de libres, un fallo de fusion dejaria el heap fragmentado para siempre). */
static void test_estres_listas(void)
{
    enum { SLOTS = 2500, OPS = 60000 };
    static slot_t s[SLOTS];
    struct kheap_counters c0 = ctr();
    size_t u0, f0, u1, f1;
    uint32_t seed = 0xBEEF1u;
    int op, corrupt_data = 0, bad_check = 0, k, rondas;

    kheap_stats(&u0, &f0);
    memset(s, 0, sizeof(s));
    for (rondas = 0; rondas < 3; ++rondas) {
        for (op = 0; op < OPS; ++op) {
            uint32_t i, n;
            seed = seed * 1664525u + 1013904223u;
            k = (int)((seed >> 8) % SLOTS);
            if (!s[k].p) {
                n = 1 + (seed >> 12) % (rondas == 1 ? 90u : 700u);        /* ronda 1: solo pequenos (bins exactos) */
                if ((seed & 0x7F) == 0) n = 1100 + (seed >> 14) % 9000;   /* y de vez en cuando medianos/grandes */
                s[k].p = (uint8_t *)kmalloc(n);
                if (!s[k].p) continue;
                s[k].n = n; s[k].tag = (uint8_t)(seed >> 4);
                for (i = 0; i < n; ++i) s[k].p[i] = (uint8_t)(s[k].tag + i);
            } else {
                if (!slot_ok(&s[k])) corrupt_data++;
                kfree(s[k].p);
                s[k].p = 0;
            }
            if (op % 100 == 0 && kheap_check() != 0) bad_check++;
        }
        /* vaciar en orden desordenado (salto coprimo) para forzar fusiones por delante y por detras */
        for (k = 0, op = 0; op < SLOTS; ++op, k = (k + 997) % SLOTS) {
            if (s[k].p) { if (!slot_ok(&s[k])) corrupt_data++; kfree(s[k].p); s[k].p = 0; }
        }
        kheap_stats(&u1, &f1);
        CHECK_EQ_INT(u1, u0);                       /* sin fugas ... */
        CHECK_EQ_INT(f1, f0);                       /* ... y todo lo libre vuelve a ser un unico bloque */
        CHECK_EQ_INT(kheap_check(), 0);
    }
    CHECK_EQ_INT(corrupt_data, 0);
    CHECK_EQ_INT(bad_check, 0);
    CHECK_EQ_INT(ctr().live_blocks, c0.live_blocks);
    CHECK_EQ_INT(ctr().corruptions, c0.corruptions);
}

/* Va el primero: el autotest pide (espacio libre + 4 KiB) y necesita margen para ampliar. */
static void test_autotest_interno(void)
{
    host_out_reset();
    CHECK_EQ_INT(kheap_test_self(), 1);
    CHECK_EQ_INT(kheap_check(), 0);
}

static void test_reentrada(void)
{
    /* PARCHE 103: una llamada reentrante se rechaza y se cuenta, sin tocar las listas */
    CHECK_EQ_INT(kheap_test_reentry(), 1);
    CHECK_EQ_INT(kheap_check(), 0);
    struct kheap_counters c = ctr();
    CHECK(c.reentries >= 2);
    CHECK_EQ_INT(c.corruptions, 0);
}

int main(void)
{
    test_autotest_interno();
    test_reentrada();
    test_basico();
    test_tamanos_imposibles();
    test_coalescencia();
    test_veneno_al_liberar();
    test_liberaciones_invalidas();
    test_desbordamiento_de_un_byte();
    test_cabecera_danada();
    test_cabecera_vecina_pisada();
    test_sin_memoria_inyectado();
    test_tope_de_arena();
    test_estres();
    test_estres_listas();
    TEST_MAIN_END("heap");
}
