/* PARCHE 083: pruebas del codificador PNG. Escribe PNG en build/tests/ que comprueba Python (zlib y PIL) desde 'make test'. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/png_enc.h"

static void save(const char *name, const uint8_t *b, size_t n) { FILE *f = fopen(name, "wb"); assert(f); fwrite(b, 1, n, f); fclose(f); }

int main(void)
{
    assert(png_crc32(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u);
    assert(png_adler32((const uint8_t *)"Wikipedia", 9) == 0x11E60398u);
    int w = 320, h = 240;
    uint32_t *px = malloc((size_t)w * h * 4);
    uint8_t *out = malloc(1 << 20); void *work = malloc(png_work_size(w, h));
    /* 1: dibujo plano (fondo blanco, rectangulos y un circulo) */
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t c = 0xFFFFFF;
        if (x > 40 && x < 140 && y > 30 && y < 120) c = 0x3B82F6;
        if ((x - 220) * (x - 220) + (y - 150) * (y - 150) < 50 * 50) c = 0xEF4444;
        if (y == 200) c = 0x000000;
        px[y * w + x] = c;
    }
    size_t n = png_encode_rgb(px, w, h, out, 1 << 20, work);
    printf("dibujo 320x240: %zu bytes\n", n);
    assert(n > 0 && n < 16384);
    save("build/tests/png_dibujo.png", out, n);
    FILE *f = fopen("build/tests/png_dibujo.rgb", "wb"); for (int i = 0; i < w * h; i++) { uint8_t t[3] = { px[i] >> 16, px[i] >> 8, px[i] }; fwrite(t, 1, 3, f); } fclose(f);
    /* 2: ruido (no comprime): debe seguir siendo un PNG valido */
    unsigned s = 12345;
    for (int i = 0; i < 64 * 48; i++) { s = s * 1103515245u + 12345u; px[i] = s >> 8; }
    n = png_encode_rgb(px, 64, 48, out, 1 << 20, work);
    assert(n > 64 * 48 * 3);
    save("build/tests/png_ruido.png", out, n);
    f = fopen("build/tests/png_ruido.rgb", "wb"); for (int i = 0; i < 64 * 48; i++) { uint8_t t[3] = { px[i] >> 16, px[i] >> 8, px[i] }; fwrite(t, 1, 3, f); } fclose(f);
    /* 3: degradado (usa los filtros Sub/Up) */
    for (int y = 0; y < 100; y++) for (int x = 0; x < 200; x++) px[y * 200 + x] = (uint32_t)(x << 16 | y << 8 | ((x + y) & 255));
    n = png_encode_rgb(px, 200, 100, out, 1 << 20, work);
    assert(n > 0 && n < 200 * 100);
    save("build/tests/png_degradado.png", out, n);
    f = fopen("build/tests/png_degradado.rgb", "wb"); for (int i = 0; i < 200 * 100; i++) { uint8_t t[3] = { px[i] >> 16, px[i] >> 8, px[i] }; fwrite(t, 1, 3, f); } fclose(f);
    /* sin sitio: 0 */
    assert(png_encode_rgb(px, 200, 100, out, 100, work) == 0);
    /* zlib suelto: texto repetitivo */
    { const char *t = "SOMA SOMA SOMA SOMA SOMA SOMA SOMA SOMA abcabcabcabc"; size_t zn = zlib_compress((const uint8_t *)t, strlen(t), out, 1000, work);
      assert(zn > 6 && zn < strlen(t)); save("build/tests/zlib_texto.z", out, zn); }
    puts("test_png: OK (falta la comprobacion de Python)");
    return 0;
}
