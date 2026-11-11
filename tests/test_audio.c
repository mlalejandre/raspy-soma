/* Pruebas en host del nucleo de audio REAL (src/core/audio.c) con un DMA simulado que lee el anillo en bucle.
 * Propiedad central: lo que el DMA consume = ceros (silencio) + exactamente los datos escritos, en orden, sin
 * perdidas, sin duplicados y sin repetir NUNCA un trozo viejo (los datos de prueba no tienen ningun byte 0). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "audio.h"

#define R 65536u

struct sim {
    uint8_t ring[R];
    uint32_t pos;                  /* byte que lee el DMA */
    int started, stops, start_calls;
    uint32_t start_rate;
    uint8_t *out; size_t out_len, out_cap;     /* todo lo que el DMA ha consumido */
};
static struct sim S;
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }

static int h_start(void *c, uint32_t rate) { (void)c; S.started = 1; S.start_calls++; S.start_rate = rate; return 0; }
static void h_stop(void *c) { (void)c; S.started = 0; S.stops++; }
static uint32_t h_pos(void *c) { (void)c; return S.pos; }

static void consume(uint32_t n)             /* el DMA lee n bytes del anillo */
{
    for (uint32_t i = 0; i < n; ++i) {
        if (S.out_len == S.out_cap) { S.out_cap = S.out_cap ? S.out_cap * 2 : 1 << 20; S.out = realloc(S.out, S.out_cap); }
        S.out[S.out_len++] = S.started ? S.ring[S.pos] : 0;
        S.pos = (S.pos + 1) % R;
    }
}

static uint8_t fifo[32768];
static struct audio A;
static void setup(uint32_t pos0)
{
    memset(S.ring, 0xEE, sizeof S.ring);                    /* basura: audio_open debe dejar el anillo a cero */
    S.pos = pos0; S.started = 0; S.stops = 0; S.start_calls = 0; S.out_len = 0;
    struct audio_hw hw = { 0, S.ring, R, h_start, h_stop, h_pos };
    audio_init(&A, &hw, fifo, sizeof fifo);
}

/* byte de datos k: nunca 0 */
static uint8_t databyte(uint64_t k) { return (uint8_t)(1 + (k * 7 + (k >> 8)) % 255); }

/* comprueba: los bytes no nulos de la salida == los datos aceptados, en orden */
static int check_stream(uint64_t accepted, const char *what)
{
    uint64_t k = 0; int ok = 1;
    for (size_t i = 0; i < S.out_len; ++i) {
        if (S.out[i] == 0) continue;
        if (k >= accepted || S.out[i] != databyte(k)) { ok = 0; printf("  %s: byte %llu de datos mal en la salida (pos %zu)\n", what, (unsigned long long)k, i); break; }
        k++;
    }
    if (ok && k != accepted) { ok = 0; printf("  %s: salieron %llu bytes de datos, se escribieron %llu\n", what, (unsigned long long)k, (unsigned long long)accepted); }
    return ok;
}

/* productor: intenta escribir `want` bytes (siguiendo databyte) */
static uint64_t g_accepted;
static uint32_t produce(uint32_t want)
{
    uint8_t buf[8192]; if (want > sizeof buf) want = sizeof buf;
    for (uint32_t i = 0; i < want; ++i) buf[i] = databyte(g_accepted + i);
    uint32_t n = audio_write(&A, buf, want);
    g_accepted += n;
    return n;
}

static void test_basico(void)
{
    setup(0);
    CHECK_EQ_INT(audio_open(&A, 100), -1);                 /* frecuencia absurda */
    CHECK_EQ_INT(audio_open(&A, 44100), 0);
    CHECK_EQ_INT(audio_open(&A, 44100), -1);               /* ya abierto */
    int ring_cero = 1; for (uint32_t i = 0; i < R; ++i) if (S.ring[i]) ring_cero = 0;
    CHECK(ring_cero);
    CHECK_EQ_INT(audio_space(&A), sizeof fifo);
    uint8_t b[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    CHECK_EQ_INT(audio_write(&A, b, 7), 4);                /* solo frames enteros */
    CHECK_EQ_INT(audio_queued(&A), 4);
    CHECK(!audio_idle(&A));
    audio_pump(&A);
    CHECK_EQ_INT(S.start_calls, 1); CHECK_EQ_INT(S.start_rate, 44100);   /* arranca con el primer dato */
    audio_pump(&A); CHECK_EQ_INT(S.start_calls, 1);
    CHECK_EQ_INT(audio_queued(&A), 0);
    consume(2000); audio_pump(&A);
    CHECK(audio_idle(&A));
    audio_close(&A); CHECK_EQ_INT(S.stops, 1);
    CHECK_EQ_INT(audio_write(&A, b, 4), 0);                /* cerrado */
    CHECK_EQ_INT(audio_open(&A, 48000), 0);                /* se puede reabrir */
    audio_close(&A);
    setup(0);
    CHECK_EQ_INT(audio_write(&A, b, 4), 0);                /* sin abrir */
    audio_pump(&A); CHECK_EQ_INT(S.start_calls, 0);
}

static void run_stream(uint32_t pos0, uint64_t total, uint32_t cons_max, uint32_t prod_max, const char *name, uint32_t expect_under)
{
    setup(pos0); g_accepted = 0;
    CHECK_EQ_INT(audio_open(&A, 44100), 0);
    uint64_t sent = 0; int guard = 0;
    sent += produce(8192);                                 /* un reproductor precarga antes de arrancar */
    while ((sent < total || !audio_idle(&A)) && guard++ < 5000000) {
        if (sent < total) { uint32_t n = produce(prod_max ? 4 * (1 + rnd() % (prod_max / 4)) : 4096); sent += n; }
        audio_pump(&A);
        consume(4 * (rnd() % (cons_max / 4 + 1)));
    }
    consume(R * 2);                                        /* despues de acabar: solo silencio, nunca un eco */
    CHECK(check_stream(g_accepted, name));
    CHECK(g_accepted >= total);
    struct audio_stats st; audio_get_stats(&A, &st);
    if (st.underruns != expect_under) printf("  %s: %u infrautilizaciones (esperadas %u)\n", name, st.underruns, expect_under);
    CHECK_EQ_INT(st.underruns, expect_under);
    CHECK_EQ_INT(st.data_bytes, g_accepted);
    audio_close(&A);
}

static void test_flujo_continuo(void)
{
    /* el consumo (hasta 3000 B por vuelta, media 1500) es MENOR que la produccion: nunca falta dato */
    run_stream(0, 20ull * R, 3000, 6000, "continuo", 0);
    run_stream(1000, 20ull * R, 3000, 6000, "continuo, DMA arrancado en 1000", 0);
    run_stream(R - 8, 7ull * R, 3000, 6000, "continuo, DMA arrancado casi al final del anillo", 0);
    run_stream(0, 200000, 1500, 0, "continuo, escrituras de 4096", 0);
    run_stream(0, 3ull * R, 8000, 16000, "bombeo espaciado (hasta 8000 B entre bombeos, el objetivo son 16 KiB)", 0);
}

static void test_inanicion_y_reanudar(void)
{
    setup(0); g_accepted = 0;
    audio_open(&A, 44100);
    uint64_t sent = 0;
    /* primer tramo */
    for (int i = 0; i < 400; ++i) { if (sent < 30000) sent += produce(2000); audio_pump(&A); consume(1200); }
    /* hueco: el productor calla mientras el DMA sigue (mas de dos vueltas) */
    for (int i = 0; i < 200; ++i) { audio_pump(&A); consume(1500); }
    struct audio_stats st; audio_get_stats(&A, &st);
    CHECK_EQ_INT(st.underruns, 0);                          /* sin dato nuevo, el silencio no es un fallo */
    /* reanuda */
    for (int i = 0; i < 600; ++i) { if (sent < 70000) sent += produce(2000); audio_pump(&A); consume(1200); }
    for (int i = 0; i < 100; ++i) { audio_pump(&A); consume(1200); }
    consume(2 * R);
    audio_get_stats(&A, &st);
    CHECK_EQ_INT(st.underruns, 1);                          /* el dato llego tarde: un hueco */
    CHECK(check_stream(g_accepted, "inanicion"));           /* y no se pierde ni se repite nada */
    /* el silencio del hueco son ceros: hay una racha larga de ceros en la salida */
    size_t best = 0, cur = 0; for (size_t i = 0; i < S.out_len; ++i) { cur = S.out[i] == 0 ? cur + 1 : 0; if (cur > best) best = cur; }
    CHECK(best > 100000);
    audio_close(&A);
}

/* un bombeo muy tardio (el DMA da casi una vuelta entera sin que nadie bombee): nada de eco ni de datos repetidos */
static void test_bombeo_tardio(void)
{
    setup(0); g_accepted = 0;
    audio_open(&A, 44100);
    produce(8000); audio_pump(&A);                         /* se colocan ~8000 B de datos */
    consume(R * 9 / 10);                                   /* el DMA los reproduce y sigue casi hasta dar la vuelta */
    audio_pump(&A);
    consume(3 * R);                                        /* varias vueltas mas: tiene que ser silencio, no el eco de los datos */
    audio_pump(&A); consume(3 * R);
    CHECK(check_stream(g_accepted, "bombeo tardio (sin eco)"));
    int guard = 0; produce(4000);
    while (!audio_idle(&A) && guard++ < 10000) { audio_pump(&A); consume(1500); }
    consume(2 * R);
    CHECK(check_stream(g_accepted, "bombeo tardio, y se reanuda"));
    audio_close(&A);
}

static void test_fifo_llena(void)
{
    setup(0); g_accepted = 0;
    audio_open(&A, 48000);
    uint32_t n1 = produce(8192), tot = 0;
    for (int i = 0; i < 10; ++i) tot += produce(8192);
    CHECK_EQ_INT(n1 + tot, sizeof fifo);                    /* acepta hasta llenar la FIFO */
    CHECK_EQ_INT(produce(4), 0);                            /* llena: no acepta mas */
    CHECK_EQ_INT(audio_space(&A), 0);
    int guard = 0;
    while (!audio_idle(&A) && guard++ < 100000) { audio_pump(&A); consume(900); }
    CHECK(audio_idle(&A));
    consume(2 * R);
    CHECK(check_stream(g_accepted, "fifo llena"));
    audio_close(&A);
}

static void test_sin_eco(void)
{
    setup(0); g_accepted = 0;
    audio_open(&A, 44100);
    produce(8000);
    for (int i = 0; i < 100; ++i) { audio_pump(&A); consume(500); }
    size_t mark = S.out_len;
    for (int i = 0; i < 400; ++i) { audio_pump(&A); consume(700); }     /* 4 vueltas del anillo despues de acabar */
    int silencio = 1; for (size_t i = mark; i < S.out_len; ++i) if (S.out[i]) silencio = 0;
    CHECK(silencio);
    audio_close(&A);
}

static void test_reapertura_limpia(void)
{
    setup(0); g_accepted = 0;
    audio_open(&A, 44100); produce(20000);
    for (int i = 0; i < 20; ++i) { audio_pump(&A); consume(800); }
    audio_close(&A);
    /* reabrir: ni la FIFO ni el anillo conservan nada de la sesion anterior */
    memset(S.ring, 0x55, sizeof S.ring);
    CHECK_EQ_INT(audio_open(&A, 22050), 0);
    CHECK_EQ_INT(audio_queued(&A), 0);
    int cero = 1; for (uint32_t i = 0; i < R; ++i) if (S.ring[i]) cero = 0; CHECK(cero);
    audio_close(&A);
}

static void test_fuzz(void)
{
    /* operaciones al azar (escribir, bombear, consumir, parones) contra la propiedad central */
    for (int round = 0; round < 40; ++round) {
        setup((rnd() % (R / 4)) * 4); g_accepted = 0;
        audio_open(&A, 8000 + rnd() % 100000);
        uint32_t since = 0;                       /* bytes consumidos desde el ultimo bombeo */
        for (int step = 0; step < 4000; ++step) {
            switch (rnd() % 6) {
            case 0: case 1: produce(4 * (1 + rnd() % 2000)); break;
            case 2: case 3: audio_pump(&A); since = 0; break;
            case 4: { uint32_t n = 4 * (rnd() % 2500); if (since + n > R / 2) { audio_pump(&A); since = 0; } consume(n); since += n; } break;
            case 5: if (rnd() % 20 == 0) { for (int k = 0; k < 30; ++k) { audio_pump(&A); consume(2000); } since = 0; } break;   /* paron del productor */
            }
        }
        int guard = 0; while (!audio_idle(&A) && guard++ < 200000) { audio_pump(&A); consume(1000); }
        consume(2 * R);
        CHECK(check_stream(g_accepted, "fuzz"));
        audio_close(&A);
    }
}

/* SOMA 022: el reloj de audio cuenta solo datos del usuario que ya sonaron (no el silencio de un hueco) */
static void test_reloj(void)
{
    setup(1000);
    CHECK_EQ_INT(audio_open(&A, 48000), 0);
    CHECK(audio_media_us(&A) == 0);
    uint8_t buf[4096]; for (uint32_t i = 0; i < sizeof buf; ++i) buf[i] = (uint8_t)(1 + i % 200);
    uint64_t prev = 0; uint64_t written = 0;
    for (int it = 0; it < 120; ++it) {
        written += audio_write(&A, buf, sizeof buf);
        audio_pump(&A);
        consume(3000); audio_pump(&A);
        uint64_t m = audio_media_us(&A);
        CHECK(m >= prev); prev = m;
        uint64_t dma_us = (uint64_t)S.out_len * 1000000ULL / (48000ULL * 4);
        CHECK(m <= dma_us + 1000);
        CHECK(m + 6000 >= dma_us || S.out_len < 4000);
    }
    /* hueco: se deja de escribir mientras suena silencio, luego se reanuda */
    uint64_t antes = audio_media_us(&A);
    for (int i = 0; i < 40; ++i) { consume(2000); audio_pump(&A); }
    CHECK(audio_media_us(&A) >= antes);
    CHECK(audio_media_us(&A) <= antes + 1000000ULL * (written - 0) / (48000ULL * 4));
    uint64_t data_us_total = written * 1000000ULL / (48000ULL * 4);
    CHECK(audio_media_us(&A) <= data_us_total);               /* nunca cuenta mas datos de los escritos */
    audio_write(&A, buf, sizeof buf); audio_pump(&A); consume(1024); audio_pump(&A);
    uint64_t despues = audio_media_us(&A);
    CHECK(despues <= data_us_total + 1000000ULL * sizeof buf / (48000ULL * 4));
    CHECK_EQ_INT(A.st.underruns, 1);
    audio_close(&A);
    CHECK(audio_media_us(&A) == 0);
}

/* SOMA 023: audio_flush descarta solo lo no colocado y el flujo sigue limpio despues */
static void test_flush(void)
{
    setup(500);
    CHECK_EQ_INT(audio_open(&A, 44100), 0);
    CHECK_EQ_INT(audio_flush(&A), 0);                      /* nada que descartar */
    uint8_t b[4096]; for (uint32_t i = 0; i < sizeof b; ++i) b[i] = (uint8_t)(1 + i % 200);
    uint32_t w1 = audio_write(&A, b, sizeof b); audio_pump(&A);
    uint32_t w2 = audio_write(&A, b, sizeof b);            /* esto queda en la FIFO si el anillo ya esta lleno hasta target */
    uint32_t q = audio_queued(&A);
    CHECK_EQ_INT(audio_flush(&A), q);
    CHECK_EQ_INT(audio_queued(&A), 0);
    CHECK_EQ_INT(audio_space(&A), sizeof fifo);
    (void)w1; (void)w2;
    consume(2000); audio_pump(&A);
    uint32_t w3 = audio_write(&A, b, 1024); CHECK_EQ_INT(w3, 1024);
    audio_pump(&A); consume(20000); audio_pump(&A);
    CHECK(audio_idle(&A));
    audio_close(&A);
    CHECK_EQ_INT(audio_flush(&A), 0);                      /* cerrado */
}

int main(void)
{
    test_basico();
    test_flujo_continuo();
    test_inanicion_y_reanudar();
    test_bombeo_tardio();
    test_fifo_llena();
    test_sin_eco();
    test_reapertura_limpia();
    test_fuzz();
    test_reloj();
    test_flush();
    free(S.out);
    TEST_MAIN_END("audio");
}
