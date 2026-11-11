/* Pruebas en host de src/core/panic.c: recorrido de frames y vigilante.
 * Se compila con -DSOMA_HOST_PANIC (limites de .text simulados). */
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "panic.h"

uint64_t host_text_lo = 0x100000, host_text_hi = 0x200000;

/* Memoria simulada: 4 KiB a partir de BASE. Solo se puede leer ahi y alineado. */
#define BASE 0x10000ULL
static uint64_t mem[512];

static int rd(uint64_t addr, uint64_t *out)
{
    if (addr < BASE || addr + 8 > BASE + sizeof(mem) || (addr & 7)) return 0;
    *out = mem[(addr - BASE) / 8];
    return 1;
}

static void frame(uint64_t at, uint64_t next, uint64_t ret)
{
    mem[(at - BASE) / 8] = next;
    mem[(at - BASE) / 8 + 1] = ret;
}

static void test_cadena_normal(void)
{
    uint64_t r[PANIC_MAX_FRAMES];
    int n;

    memset(mem, 0, sizeof(mem));
    frame(BASE + 0x00, BASE + 0x40, 0x100AAA);
    frame(BASE + 0x40, BASE + 0x90, 0x100BBB);
    frame(BASE + 0x90, 0, 0x100CCC);
    n = panic_walk_frames(BASE, 0, rd, r, PANIC_MAX_FRAMES);
    CHECK_EQ_INT(n, 3);
    CHECK_EQ_INT(r[0], 0x100AAA); CHECK_EQ_INT(r[1], 0x100BBB); CHECK_EQ_INT(r[2], 0x100CCC);
}

static void test_entradas_invalidas(void)
{
    uint64_t r[PANIC_MAX_FRAMES];

    memset(mem, 0, sizeof(mem));
    frame(BASE, BASE + 0x40, 0x100AAA);
    CHECK_EQ_INT(panic_walk_frames(0, 0, rd, r, 16), 0);              /* fp nulo */
    CHECK_EQ_INT(panic_walk_frames(BASE + 4, 0, rd, r, 16), 0);       /* mal alineado */
    CHECK_EQ_INT(panic_walk_frames(BASE - 16, 0, rd, r, 16), 0);      /* ilegible */
    CHECK_EQ_INT(panic_walk_frames(BASE + sizeof(mem), 0, rd, r, 16), 0);
    CHECK_EQ_INT(panic_walk_frames(BASE, BASE + 0x80, rd, r, 16), 0); /* por debajo de sp */
    CHECK_EQ_INT(panic_walk_frames(BASE, 0, rd, r, 0), 0);            /* max = 0 */
    CHECK_EQ_INT(panic_walk_frames(BASE, BASE, rd, r, 16), 1);        /* sp justo en fp: valido */
}

static void test_cadenas_hostiles(void)
{
    uint64_t r[PANIC_MAX_FRAMES];

    memset(mem, 0, sizeof(mem));
    frame(BASE, BASE, 0x100AAA);                                      /* apunta a si mismo */
    CHECK_EQ_INT(panic_walk_frames(BASE, 0, rd, r, 16), 1);

    memset(mem, 0, sizeof(mem));
    frame(BASE + 0x40, BASE, 0x100AAA);                               /* retrocede: bucle */
    frame(BASE, BASE + 0x40, 0x100BBB);
    CHECK_EQ_INT(panic_walk_frames(BASE + 0x40, 0, rd, r, 16), 1);

    memset(mem, 0, sizeof(mem));
    frame(BASE, BASE + 0x200000, 0x100AAA);                           /* salto de 2 MiB */
    CHECK_EQ_INT(panic_walk_frames(BASE, 0, rd, r, 16), 1);

    memset(mem, 0, sizeof(mem));
    frame(BASE, BASE + 0x40, 0x100AAA);
    frame(BASE + 0x40, BASE + 0x80, 0);                               /* retorno nulo: fin */
    CHECK_EQ_INT(panic_walk_frames(BASE, 0, rd, r, 16), 1);

    memset(mem, 0, sizeof(mem));
    frame(BASE, BASE + 0x40, 0x100AAA);
    frame(BASE + 0x40, BASE + 0x100000, 0x100BBB);                    /* siguiente fuera de memoria */
    CHECK_EQ_INT(panic_walk_frames(BASE, 0, rd, r, 16), 2);
}

static void test_limite_de_frames(void)
{
    uint64_t r[PANIC_MAX_FRAMES];
    int i;

    memset(mem, 0, sizeof(mem));
    for (i = 0; i < 40; ++i) frame(BASE + (uint64_t)i * 16, BASE + (uint64_t)(i + 1) * 16, 0x100000u + (uint64_t)i);
    CHECK_EQ_INT(panic_walk_frames(BASE, 0, rd, r, PANIC_MAX_FRAMES), PANIC_MAX_FRAMES);
    CHECK_EQ_INT(r[15], 0x10000F);
    CHECK_EQ_INT(panic_walk_frames(BASE, 0, rd, r, 5), 5);
}

static void test_impresion(void)
{
    memset(mem, 0, sizeof(mem));
    frame(BASE, BASE + 0x40, 0x100AAA);          /* dentro de .text */
    frame(BASE + 0x40, 0, 0x300000);             /* fuera de .text */
    host_out_reset();
    panic_print_context_with(rd, 0x100123, 0x100456, BASE, BASE);
    CHECK(strstr(host_out(), "BACKTRACE (hilo 0") != 0);
    CHECK(strstr(host_out(), "#0  0x0000000000100123") != 0);
    CHECK(strstr(host_out(), "LR  0x0000000000100456") != 0);
    CHECK(strstr(host_out(), "#1  0x0000000000100AAA") != 0);
    CHECK(strstr(host_out(), "#2  0x0000000000300000  [fuera de .text]") != 0);
    CHECK(strstr(host_out(), "symbolize.py") != 0);

    host_out_reset();
    panic_print_context_with(rd, 0x400000, 0, 0, 0);   /* sin cadena y pc fuera de .text */
    CHECK(strstr(host_out(), "[fuera de .text]") != 0);
    CHECK(strstr(host_out(), "cadena de frames no disponible") != 0);
    CHECK(strstr(host_out(), "LR ") == 0);              /* lr = 0: no se imprime */
}

static void test_vigilante_decision(void)
{
    struct wd_state st;
    const uint32_t L = 5000;

    memset(&st, 0, sizeof(st));
    CHECK_EQ_INT(wd_evaluate(&st, 1000, 1000, L), 0);          /* primer latido visto */
    CHECK_EQ_INT(wd_evaluate(&st, 1000 + L - 1, 1000, L), 0);  /* a 1 ms del limite */
    CHECK_EQ_INT(wd_evaluate(&st, 1000 + L, 1000, L), 1);      /* limite: avisa */
    CHECK_EQ_INT(st.reports, 1); CHECK_EQ_INT(st.trips, 1);
    CHECK_EQ_INT(wd_evaluate(&st, 1000 + L + 1, 1000, L), 0);  /* sin spam */
    CHECK_EQ_INT(wd_evaluate(&st, 1000 + 2 * L - 1, 1000, L), 0);
    CHECK_EQ_INT(wd_evaluate(&st, 1000 + 2 * L, 1000, L), 1);  /* repite cada limit_ms */
    CHECK_EQ_INT(st.trips, 2);
    CHECK_EQ_INT(wd_evaluate(&st, 50000, 50000, L), 0);        /* latido nuevo: rearma */
    CHECK_EQ_INT(st.reports, 0); CHECK_EQ_INT(st.trips, 2);
    CHECK_EQ_INT(wd_evaluate(&st, 50000 + L, 50000, L), 1);    /* y vuelve a avisar */
    CHECK_EQ_INT(st.trips, 3);
    CHECK_EQ_INT(wd_evaluate(&st, 10, 50000, L), 0);           /* reloj hacia atras: nada */
}

static void test_vigilante_integrado(void)
{
    uint32_t t0;

    host_out_reset();
    watchdog_tick(999999, 0x100000, 0, 0);                     /* sin latidos: desarmado */
    CHECK_EQ_INT(host_out()[0], 0);
    watchdog_beat(2000);
    CHECK_EQ_INT(watchdog_last_beat_ms(), 2000);
    watchdog_tick(2000, 0x100000, 0, 0);                       /* registra el latido */
    watchdog_tick(2000 + WATCHDOG_LIMIT_MS - 1, 0x100000, 0, 0);
    CHECK_EQ_INT(host_out()[0], 0);                            /* antes del limite: callado */
    t0 = watchdog_trips();
    watchdog_tick(2000 + WATCHDOG_LIMIT_MS, 0x100123, 0, 0);
    CHECK(strstr(host_out(), "[WATCHDOG]") != 0);
    CHECK(strstr(host_out(), "aviso 1") != 0);
    CHECK(strstr(host_out(), "#0  0x0000000000100123") != 0);
    CHECK_EQ_INT(watchdog_trips(), t0 + 1);
    host_out_reset();
    watchdog_tick(2000 + WATCHDOG_LIMIT_MS + 10, 0x100123, 0, 0);
    CHECK_EQ_INT(host_out()[0], 0);                            /* no repite enseguida */
    watchdog_tick(2000 + 2 * WATCHDOG_LIMIT_MS, 0x100123, 0, 0);
    CHECK(strstr(host_out(), "aviso 2") != 0);
}

int main(void)
{
    test_cadena_normal();
    test_entradas_invalidas();
    test_cadenas_hostiles();
    test_limite_de_frames();
    test_impresion();
    test_vigilante_decision();
    test_vigilante_integrado();
    TEST_MAIN_END("panic");
}
