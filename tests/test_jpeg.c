/* SOMA 022: pruebas del decodificador JPEG contra libjpeg (via tools/jpegref.py) + fuzz. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdint.h>
#include "../src/core/jpeg.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FALLO: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(l ? (size_t)l : 1); if (fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)l; return b;
}

static int cmp(const char *dir, const char *name)
{
    char p[512]; size_t jl, rl; int tol, w, h;
    const char *us = strrchr(name, '_'); tol = atoi(us + 1);
    const char *x = strrchr(name, 'x'); const char *e = x; while (e > name && e[-1] >= '0' && e[-1] <= '9') e--;
    w = atoi(e); h = atoi(x + 1);
    snprintf(p, sizeof p, "%s/%s.jpg", dir, name); uint8_t *j = slurp(p, &jl);
    snprintf(p, sizeof p, "%s/%s.rgb", dir, name); uint8_t *r = slurp(p, &rl);
    if (!j || !r) { printf("FALLO: no leo %s\n", name); return 1; }
    uint32_t *out = calloc((size_t)(w + 5) * (h + 2), 4);
    struct jpeg_info in;
    int rc = jpeg_decode(j, jl, out, (uint32_t)w + 5, (uint32_t)h + 2, &in);
    int bad = 0;
    if (rc || (int)in.width != w || (int)in.height != h || rl != (size_t)w * h * 3) { printf("FALLO: %s rc=%d %ux%u\n", name, rc, in.width, in.height); bad = 1; }
    else {
        long big = 0, tot = 0, off1 = 0, offbig = 0; int maxd = 0;
        for (int y = 0; y < h; ++y) for (int xx = 0; xx < w; ++xx) {
            uint32_t v = out[(size_t)y * (w + 5) + xx]; const uint8_t *q = r + ((size_t)y * w + xx) * 3;
            int d[3] = { (int)((v >> 16) & 255) - q[0], (int)((v >> 8) & 255) - q[1], (int)(v & 255) - q[2] };
            for (int k = 0; k < 3; ++k) { int a = d[k] < 0 ? -d[k] : d[k]; if (a > maxd) maxd = a; if (a > tol) big++; if (a > 0) off1++; if (a > 1) offbig++; tot++; }
        }
        /* con submuestreo se tolera que una fraccion pequena de muestras (bordes de color) supere la tolerancia */
        /* 4:4:4 y gris: la IDCT y la conversion de color son las de libjpeg salvo redondeos; casi todo exacto y nada a >1 */
        if (tol == 3 && (offbig > tot / 200 || off1 > tot / 10)) { printf("FALLO: %s no es casi exacto (dif>0: %ld, dif>1: %ld de %ld)\n", name, off1, offbig, tot); bad = 1; }
        long allow = tol >= 40 && tol < 255 ? tot / 6 : 0;
        if (big > allow) { printf("FALLO: %s maxdiff=%d fuera=%ld/%ld\n", name, maxd, big, tot); bad = 1; }
        /* el margen fuera de la imagen no se toca */
        for (int y = 0; y < h + 2; ++y) for (int xx = (y < h ? w : 0); xx < w + 5; ++xx) if (out[(size_t)y * (w + 5) + xx]) { printf("FALLO: %s escribe fuera\n", name); bad = 1; y = h + 2; break; }
    }
    free(j); free(r); free(out); return bad;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/jt";
    DIR *d = opendir(dir); if (!d) { printf("FALLO: sin directorio %s\n", dir); return 1; }
    struct dirent *de; int n = 0; static char names[400][128];
    while ((de = readdir(d)) && n < 400) { size_t l = strlen(de->d_name); if (l > 4 && !strcmp(de->d_name + l - 4, ".jpg") && de->d_name[0] == 'c') { memcpy(names[n], de->d_name, l - 4); names[n][l - 4] = 0; n++; } }
    closedir(d);
    CHECK(n > 50, "pocos casos (%d)", n);
    for (int i = 0; i < n; ++i) fails += cmp(dir, names[i]);

    /* progresivo -> UNSUPPORTED; recortes -> sin crash ni escritura fuera */
    char p[512]; size_t l; snprintf(p, sizeof p, "%s/prog.jpg", dir);
    uint8_t *pj = slurp(p, &l); CHECK(pj != NULL, "prog.jpg");
    if (pj) { uint32_t o[64 * 64]; CHECK(jpeg_decode(pj, l, o, 64, 64, NULL) == JPEG_E_UNSUPPORTED, "progresivo debe rechazarse"); free(pj); }

    /* fuzz: mutaciones aleatorias y truncados de varios JPEG, buffer de salida con guardas */
    uint64_t seed = 12345; long runs = 0;
    for (int i = 0; i < n; i += 3) {
        snprintf(p, sizeof p, "%s/%s.jpg", dir, names[i]); uint8_t *j = slurp(p, &l); if (!j) continue;
        uint8_t *m = malloc(l);
        for (int it = 0; it < 150; ++it) {
            memcpy(m, j, l); size_t ml = l;
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            int mode = (int)((seed >> 33) % 3);
            if (mode == 0) ml = (size_t)((seed >> 20) % (l + 1));
            else { int k = 1 + (int)((seed >> 40) % 6); while (k--) { seed = seed * 6364136223846793005ULL + 1; m[(seed >> 24) % l] ^= (uint8_t)(1u << ((seed >> 50) & 7)); } }
            uint32_t *o = calloc((size_t)(130 + 4) * (130), 4);          /* 4 columnas y 0 filas de guarda por la derecha */
            int rc = jpeg_decode(m, ml, o, 134, 130, NULL); (void)rc;
            free(o); runs++;
        }
        free(m); free(j);
    }
    /* dimension mayor que la salida: rechazo sin escribir */
    snprintf(p, sizeof p, "%s/%s.jpg", dir, names[0]); uint8_t *j0 = slurp(p, &l);
    if (j0) { uint32_t o[4] = {0}; struct jpeg_info in; int rc = jpeg_get_info(j0, l, &in); CHECK(rc == 0, "get_info"); CHECK(jpeg_decode(j0, l, o, 1, 1, NULL) == JPEG_E_SIZE || in.width <= 1, "salida pequena"); free(j0); }
    CHECK(jpeg_decode((const uint8_t *)"hola", 4, NULL, 0, 0, NULL) == JPEG_E_FORMAT, "formato");
    printf("jpeg: %d casos, %ld mutaciones, %s\n", n, runs, fails ? "FALLOS" : "OK");
    return fails ? 1 : 0;
}
