/* Pruebas en host del analizador WAV real (src/core/wav.c): cabeceras validas con bloques extra y relleno, formatos
 * no soportados, truncados y fuzzing (cada byte, cada longitud) con ASan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "wav.h"

static void w16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }

/* WAV con un bloque LIST de `list_sz` bytes opcional entre fmt y data; devuelve la longitud de la cabecera */
static size_t build(uint8_t *b, uint16_t ch, uint32_t rate, uint16_t bits, uint32_t list_sz, uint32_t data_sz, int ext)
{
    size_t p = 0;
    memcpy(b, "RIFF", 4); w32(b + 4, 0); memcpy(b + 8, "WAVE", 4); p = 12;
    memcpy(b + p, "fmt ", 4); w32(b + p + 4, ext ? 40 : 16);
    w16(b + p + 8, ext ? 0xFFFE : 1); w16(b + p + 10, ch); w32(b + p + 12, rate); w32(b + p + 16, rate * ch * bits / 8);
    w16(b + p + 20, (uint16_t)(ch * bits / 8)); w16(b + p + 22, bits);
    if (ext) { w16(b + p + 24, 22); w16(b + p + 26, bits); w32(b + p + 28, 3); w16(b + p + 32, 1); memset(b + p + 34, 0, 14); }
    p += 8 + (ext ? 40 : 16);
    if (list_sz) { memcpy(b + p, "LIST", 4); w32(b + p + 4, list_sz); memset(b + p + 8, 'x', list_sz); p += 8 + list_sz + (list_sz & 1); }
    memcpy(b + p, "data", 4); w32(b + p + 4, data_sz); p += 8;
    return p;
}

static void test_validos(void)
{
    uint8_t b[256]; struct wav_info w;
    size_t n = build(b, 2, 44100, 16, 0, 1000, 0);
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_OK);
    CHECK_EQ_INT(w.channels, 2); CHECK_EQ_INT(w.rate, 44100); CHECK_EQ_INT(w.bits, 16);
    CHECK_EQ_INT(w.data_offset, 44); CHECK_EQ_INT(w.data_size, 1000);        /* la cabecera clasica de 44 bytes */
    n = build(b, 1, 22050, 16, 0, 500, 0);
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_OK); CHECK_EQ_INT(w.channels, 1); CHECK_EQ_INT(w.rate, 22050);
    n = build(b, 2, 48000, 16, 26, 4, 0);                                    /* bloque LIST de 26 bytes (par) */
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_OK); CHECK_EQ_INT(w.data_offset, 44 + 8 + 26);
    n = build(b, 2, 48000, 16, 25, 4, 0);                                    /* bloque LIST impar: lleva 1 byte de relleno */
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_OK); CHECK_EQ_INT(w.data_offset, 44 + 8 + 26);
    n = build(b, 2, 44100, 16, 0, 8, 1);                                     /* WAVE_FORMAT_EXTENSIBLE con subformato PCM */
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_OK); CHECK_EQ_INT(w.rate, 44100);
    n = build(b, 2, 8000, 16, 0, 0xFFFFFFFFu, 0);                            /* tamano 'desconocido' (streaming) */
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_OK); CHECK_EQ_INT(w.data_size, 0xFFFFFFFFu);
    CHECK_EQ_INT(wav_parse(b, n + 100 > sizeof b ? n : n, &w), WAV_OK);
}

static void test_errores(void)
{
    uint8_t b[256]; struct wav_info w; size_t n;
    n = build(b, 2, 44100, 16, 0, 10, 0); b[0] = 'X';
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_RIFF);
    n = build(b, 2, 44100, 16, 0, 10, 0); b[8] = 'X';
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_RIFF);
    CHECK_EQ_INT(wav_parse(b, 11, &w), WAV_E_SHORT);
    CHECK_EQ_INT(wav_parse(0, 100, &w), WAV_E_SHORT);
    n = build(b, 2, 44100, 8, 0, 10, 0);   CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_UNSUPPORTED);     /* 8 bits */
    n = build(b, 2, 44100, 24, 0, 10, 0);  CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_UNSUPPORTED);     /* 24 bits */
    n = build(b, 6, 44100, 16, 0, 10, 0);  CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_UNSUPPORTED);     /* 5.1 */
    n = build(b, 2, 1000, 16, 0, 10, 0);   CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_UNSUPPORTED);     /* frecuencia fuera de rango */
    n = build(b, 2, 44100, 16, 0, 10, 0); w16(b + 20, 3);                                              /* formato no PCM (3 = float) */
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_UNSUPPORTED);
    n = build(b, 2, 44100, 16, 0, 10, 0); w16(b + 32, 5);                                              /* block align incoherente */
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_FMT);
    n = build(b, 2, 44100, 16, 0, 10, 0); w32(b + 28, 1);                                              /* byte rate incoherente */
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_FMT);
    n = build(b, 2, 44100, 16, 0, 10, 0); w32(b + 16, 8);                                              /* fmt demasiado corto */
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_FMT);
    /* data antes de fmt */
    memcpy(b, "RIFF\0\0\0\0WAVEdata", 16); w32(b + 16, 4); memset(b + 20, 0, 4);
    CHECK_EQ_INT(wav_parse(b, 24, &w), WAV_E_FMT);
    /* sin data */
    n = build(b, 2, 44100, 16, 0, 10, 0); n -= 8;
    CHECK_EQ_INT(wav_parse(b, n, &w), WAV_E_NODATA);
    /* bloque con tamano gigante: no se sale del buffer ni se desborda */
    n = build(b, 2, 44100, 16, 0, 10, 0); memcpy(b + 36, "LIST", 4); w32(b + 40, 0xFFFFFFF0u);
    int r = wav_parse(b, n, &w); CHECK(r == WAV_E_SHORT || r == WAV_E_NODATA);
}

static void check_inv(const struct wav_info *w)
{
    CHECK(w->channels == 1 || w->channels == 2);
    CHECK_EQ_INT(w->bits, 16);
    CHECK(w->rate >= 4000 && w->rate <= 192000);
    CHECK(w->data_offset >= 20);
}

static void test_fuzz(void)
{
    uint8_t base[128]; size_t n = build(base, 2, 44100, 16, 6, 100, 0); int ok = 0, bad = 0; struct wav_info w;
    for (size_t pos = 0; pos < n; ++pos)
        for (int v = 0; v < 256; ++v) {
            uint8_t *b = malloc(n); memcpy(b, base, n); b[pos] = (uint8_t)v;
            int r = wav_parse(b, n, &w); if (r == WAV_OK) { ok++; check_inv(&w); CHECK(w.data_offset <= n); } else bad++;
            free(b);
        }
    for (size_t l = 0; l <= n; ++l) { uint8_t *b = malloc(l ? l : 1); memcpy(b, base, l); int r = wav_parse(b, l, &w); if (r == WAV_OK) { ok++; check_inv(&w); } else bad++; free(b); }
    unsigned s = 7; for (int it = 0; it < 30000; ++it) { uint8_t *b = malloc(n); memcpy(b, base, n);
        for (int k = 0; k < 5; ++k) { s = s * 1103515245u + 12345u; b[(s >> 8) % n] = (uint8_t)(s >> 16); }
        int r = wav_parse(b, n, &w); if (r == WAV_OK) { ok++; check_inv(&w); } else bad++; free(b); }
    CHECK(ok > 0 && bad > 0);
    printf("  fuzz: %d aceptadas (con invariantes), %d rechazadas\n", ok, bad);
}

int main(void)
{
    test_validos();
    test_errores();
    test_fuzz();
    TEST_MAIN_END("wav");
}
