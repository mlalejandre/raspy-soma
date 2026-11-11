/* PARCHE 050: pruebas del lector FAT con imagenes de tools/mkfat.py (FAT12/16/32, MBR o sin el, nombres largos, ficheros fragmentados)
 * y fuzzing de imagenes danadas. Uso: test_fat <directorio con fat12.img, fat12.txt, ...> [iteraciones de fuzz] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/core/fat.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct img { uint8_t *d; size_t n; unsigned reads; };
static int rd(void *ctx, uint64_t lba, uint32_t n, void *buf)
{
    struct img *im = ctx;
    if (n > 128 || (lba + n) * 512 > im->n) return -1;                 /* el dispositivo real tambien rechaza lo que se sale */
    memcpy(buf, im->d + lba * 512, (size_t)n * 512); im->reads++;
    return 0;
}
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;
}
static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { free(b); b = 0; } fclose(f); return b;
}

static void check_image(const char *dir, const char *name, int type, int part)
{
    char p[512]; size_t n = 0;
    snprintf(p, sizeof p, "%s/%s.img", dir, name); struct img im = { slurp(p, &n), n, 0 };
    if (!im.d) { printf("FALLO no se pudo leer %s\n", p); fails++; return; }
    struct fat_io io = { rd, &im, n / 512 }; static struct fat_vol v;
    CHECK(fat_mount(&v, &io) == 0);
    CHECK(v.type == type && v.part_index == part);
    snprintf(p, sizeof p, "%s/%s.txt", dir, name);
    FILE *m = fopen(p, "r"); char line[1024]; int files = 0, dirs = 0;
    while (m && fgets(line, sizeof line, m)) {
        char *t[4] = { 0 }; int k = 0; line[strcspn(line, "\n")] = 0;
        for (char *s = strtok(line, "\t"); s && k < 4; s = strtok(0, "\t")) t[k++] = s;
        if (t[0][0] == 'F') {
            size_t size = strtoul(t[2], 0, 10); uint32_t crc = (uint32_t)strtoul(t[3], 0, 16);
            struct fat_dirent d; CHECK(fat_stat(&v, t[1], &d) == 0 && d.size == size && !d.is_dir);
            uint8_t *buf = malloc(size + 1); CHECK(fat_read(&v, t[1], 0, buf, (uint32_t)size + 1) == (int)size && crc32(buf, size) == crc);
            /* lectura troceada con desplazamientos y tamanos raros */
            uint8_t *b2 = malloc(size + 1); size_t pos = 0; unsigned seed = (unsigned)size * 7 + 3;
            while (pos < size) { seed = seed * 1103515245u + 12345u; uint32_t c = 1 + (seed >> 8) % 3000; int g = fat_read(&v, t[1], (uint32_t)pos, b2 + pos, c); CHECK(g > 0); if (g <= 0) break; pos += (size_t)g; }
            CHECK(pos == size && (size == 0 || memcmp(buf, b2, size) == 0));
            CHECK(fat_read(&v, t[1], (uint32_t)size, buf, 10) == 0);
            /* mayusculas/minusculas distintas */
            char up[512]; strcpy(up, t[1]); for (char *c = up; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
            CHECK(fat_stat(&v, up, &d) == 0 && d.size == size);
            free(buf); free(b2); files++;
        } else {
            static struct fat_dirent ents[64]; int total = 0;
            int got = fat_readdir(&v, t[1], ents, 64, &total);
            CHECK(got >= 0 && total == atoi(t[2]) && got == total);
            for (int i = 0; i < got; i++) CHECK(ents[i].name[0] != 0 && strcmp(ents[i].name, ".") != 0 && strcmp(ents[i].name, "..") != 0);
            dirs++;
        }
    }
    if (m) fclose(m);
    CHECK(files >= 8 && dirs >= 4);
    struct fat_dirent d;
    CHECK(fat_stat(&v, "/no/existe", &d) == FAT_E_NOENT);
    CHECK(fat_stat(&v, "/HELLO.TXT/dentro", &d) == FAT_E_NOTDIR);
    CHECK(fat_read(&v, "/Carpeta", 0, &d, 1) == FAT_E_ISDIR);
    CHECK(fat_stat(&v, "", &d) == 0 && d.is_dir);
    printf("  %-7s FAT%d%s: %d ficheros y %d directorios verificados (%u lecturas)\n", name, type, part ? " con MBR" : "", files, dirs, im.reads);
    free(im.d);
}

/* tolerancia a imagenes danadas: ningun acceso fuera de limites ni bucle infinito (ASAN+UBSAN lo vigilan) */
static void fuzz_walk(struct fat_vol *v, const char *path, int depth)
{
    static struct fat_dirent ents[32]; int total = 0;
    int got = fat_readdir(v, path, ents, 32, &total);
    if (got <= 0 || depth > 4) return;
    for (int i = 0; i < got; i++) {
        char sub[600]; snprintf(sub, sizeof sub, "%s/%s", path, ents[i].name);
        if (ents[i].is_dir) fuzz_walk(v, sub, depth + 1);
        else { static uint8_t b[70000]; fat_read(v, sub, 0, b, sizeof b); fat_read(v, sub, 4097, b, 100); }
    }
}

static void fuzz(const char *dir, const char *name, int iters)
{
    char p[512]; size_t n = 0;
    snprintf(p, sizeof p, "%s/%s.img", dir, name); uint8_t *orig = slurp(p, &n);
    if (!orig) { fails++; return; }
    uint8_t *w = malloc(n); srand(12345); int mounted = 0;
    for (int it = 0; it < iters; it++) {
        memcpy(w, orig, n);
        int muts = 1 + rand() % 12;
        for (int k = 0; k < muts; k++) {
            size_t pos;
            switch (rand() % 4) {                                     /* sectores de control: arranque, FAT, directorios */
            case 0: pos = rand() % 1024; break;
            case 1: pos = 446 + rand() % 66; break;
            case 2: pos = (2048 + (size_t)(rand() % 64)) * 512 + rand() % 512; break;
            default: pos = (size_t)rand() % (n < 600000 ? n : 600000); break;
            }
            if (pos < n) w[pos] = (uint8_t)rand();
        }
        struct img im = { w, n, 0 }; struct fat_io io = { rd, &im, n / 512 }; static struct fat_vol v;
        if (fat_mount(&v, &io) == 0) { mounted++; fuzz_walk(&v, "", 0); }
    }
    printf("  fuzz %-7s: %d imagenes danadas, %d montaron y se recorrieron sin fallos\n", name, iters, mounted);
    free(orig); free(w);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/tests/fat"; int iters = argc > 2 ? atoi(argv[2]) : 1500;
    check_image(dir, "fat12", 12, 0);
    check_image(dir, "fat16", 16, 1);
    check_image(dir, "fat16s", 16, 1);
    check_image(dir, "fat32", 32, 1);
    /* casos de montaje fallidos */
    { uint8_t z[512 * 8] = { 0 }; struct img im = { z, sizeof z, 0 }; struct fat_io io = { rd, &im, 8 }; static struct fat_vol v; CHECK(fat_mount(&v, &io) == FAT_E_NOFAT);
      z[510] = 0x55; z[511] = 0xAA; CHECK(fat_mount(&v, &io) == FAT_E_NOFAT);
      z[446 + 4] = 0xEE; z[446 + 8] = 1; z[446 + 12] = 7; CHECK(fat_mount(&v, &io) == FAT_E_UNSUP); }
    fuzz(dir, "fat12", iters / 2); fuzz(dir, "fat16", iters); fuzz(dir, "fat32", iters / 3);
    printf(fails ? "fat: %d FALLOS\n" : "fat: todo correcto\n", fails);
    return fails != 0;
}
