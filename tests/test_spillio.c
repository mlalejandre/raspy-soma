/* PARCHE 113: cortes de energia en la actualizacion de archivos grandes de RamFS (BIGFS). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/fs/spillio.h"

struct ram { uint8_t *d; uint64_t sectors; long writes, crash_after; uint64_t clock; };
static int r_read(void *c, uint64_t lba, void *b, uint32_t n) { struct ram *r = c; if (lba + n > r->sectors) return -1; memcpy(b, r->d + lba * 512, (size_t)n * 512); return 0; }
static int r_write(void *c, uint64_t lba, const void *b, uint32_t n)
{
    struct ram *r = c; if (lba + n > r->sectors) return -1;
    for (uint32_t i = 0; i < n; ++i) { long idx = r->writes++; if (r->crash_after >= 0 && idx >= r->crash_after) continue; memcpy(r->d + (lba + i) * 512, (const uint8_t *)b + (size_t)i * 512, 512); }
    return 0;
}
static uint64_t r_now(void *c) { return ++((struct ram *)c)->clock; }
static struct bigfs_dev mkdev(struct ram *r) { struct bigfs_dev d; memset(&d, 0, sizeof d); d.ctx = r; d.read = r_read; d.write = r_write; d.total_sectors = r->sectors; d.alloc = malloc; d.free = free; d.now = r_now; return d; }
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
static void fill(uint8_t *b, uint32_t n, uint32_t seed) { for (uint32_t i = 0; i < n; ++i) b[i] = (uint8_t)((i * 2654435761u + seed * 40503u) >> 13); }

/* 'a' ausente, 'o' entero viejo, 'n' entero nuevo, '?' a medias */
static char classify(struct bigfs *fs, const char *path, const uint8_t *od, uint32_t ol, const char *on, const uint8_t *nd, uint32_t nl, const char *nn)
{
    uint32_t ino, len;
    if (bigfs_lookup(fs, path, &ino) < 0) return 'a';
    if (!spill_valid(fs, path, &len)) return '?';
    static uint8_t buf[70000], hb[64]; uint8_t ex[64];
    if (bigfs_read(fs, ino, 0, hb, 64) != 64) return '?';
    if (len && bigfs_read(fs, ino, 64, buf, len) != (int64_t)len) return '?';
    for (int pass = 0; pass < 2; ++pass) {
        const uint8_t *d = pass ? nd : od; uint32_t l = pass ? nl : ol; const char *nm = pass ? nn : on;
        if (!d && l == 0 && pass == 0 && ol == 0 && !on) continue;
        spill_hdr_build(ex, nm, 48, l);
        if (len == l && !memcmp(hb, ex, 64) && (l == 0 || !memcmp(buf, d, l))) return pass ? 'n' : 'o';
    }
    return '?';
}

/* ---- PARCHE 115: disco con CACHE VOLATIL: lo escrito sin flush puede perderse o llegar en cualquier orden ---- */
#define CS 4096
struct cache { uint8_t *live, *dur; struct { uint64_t lba; uint8_t d[512]; } *pend; int np, cap; long writes, budget; uint64_t clock; };
static int k_read(void *c, uint64_t lba, void *b, uint32_t n) { struct cache *r = c; if (lba + n > CS) return -1; memcpy(b, r->live + lba * 512, (size_t)n * 512); return 0; }
static int k_write(void *c, uint64_t lba, const void *b, uint32_t n)
{
    struct cache *r = c; if (lba + n > CS) return -1;
    for (uint32_t i = 0; i < n; ++i) {
        long idx = r->writes++; if (r->budget >= 0 && idx >= r->budget) continue;                 /* a partir del corte no llega nada */
        memcpy(r->live + (lba + i) * 512, (const uint8_t *)b + (size_t)i * 512, 512);
        if (r->np == r->cap) { r->cap = r->cap ? r->cap * 2 : 1024; r->pend = realloc(r->pend, (size_t)r->cap * sizeof *r->pend); }
        r->pend[r->np].lba = lba + i; memcpy(r->pend[r->np].d, (const uint8_t *)b + (size_t)i * 512, 512); r->np++;
    }
    return 0;
}
static int k_flush(void *c) { struct cache *r = c; for (int i = 0; i < r->np; ++i) memcpy(r->dur + r->pend[i].lba * 512, r->pend[i].d, 512); r->np = 0; return 0; }
static uint64_t k_now(void *c) { return ++((struct cache *)c)->clock; }
static struct bigfs_dev kdev(struct cache *r, int barriers) { struct bigfs_dev d; memset(&d, 0, sizeof d); d.ctx = r; d.read = k_read; d.write = k_write; d.total_sectors = CS; d.alloc = malloc; d.free = free; d.now = k_now; d.flush = barriers ? k_flush : 0; return d; }
static uint64_t crs = 88172645463325252ull;
static uint64_t crnd(void) { crs ^= crs << 13; crs ^= crs >> 7; crs ^= crs << 17; return crs; }

static int cache_run(int barriers, long *runs_out)
{
    static uint8_t od[60000], nd[60000]; const char *SP = "/slot", *TP = "/tmp1"; int bad = 0; long runs = 0;
    uint32_t sizes[][2] = { {20000, 30000}, {40000, 17000}, {5000, 50000}, {0, 20000} };
    for (int first = 0; first < 2; ++first)
    for (unsigned c = 0; c < sizeof sizes / sizeof sizes[0]; ++c) {
        uint32_t ol = sizes[c][0], nl = sizes[c][1]; fill(od, ol, 1); fill(nd, nl, 2);
        struct cache b; memset(&b, 0, sizeof b); b.live = calloc(CS, 512); b.dur = calloc(CS, 512); b.budget = -1;
        struct bigfs_dev d0 = kdev(&b, barriers); int e; bigfs_format(&d0, 64); k_flush(&b); memcpy(b.live, b.dur, (size_t)CS * 512);
        struct bigfs *fs = bigfs_mount(&d0, &e);
        if (!first) spill_put_atomic(fs, SP, TP, "viejo.bin", 48, (const char *)od, ol);
        bigfs_unmount(fs); k_flush(&b); memcpy(b.live, b.dur, (size_t)CS * 512);
        uint8_t *keep = malloc((size_t)CS * 512); memcpy(keep, b.dur, (size_t)CS * 512);
        b.writes = 0; fs = bigfs_mount(&d0, &e); spill_put_atomic(fs, SP, TP, "nuevo.bin", 48, (const char *)nd, nl); long total = b.writes; bigfs_unmount(fs);
        for (long cut = 0; cut <= total; cut += (total > 120 ? 1 : 1)) {
            for (int trial = 0; trial < 10; ++trial) {
                memcpy(b.dur, keep, (size_t)CS * 512); memcpy(b.live, keep, (size_t)CS * 512); b.np = 0; b.writes = 0; b.budget = cut;
                fs = bigfs_mount(&d0, &e); spill_put_atomic(fs, SP, TP, "nuevo.bin", 48, (const char *)nd, nl); bigfs_unmount(fs);
                struct ram r = { calloc(CS, 512), CS, 0, -1, 0 };                   /* disco tras el corte: durable + subconjunto arbitrario de lo pendiente */
                memcpy(r.d, b.dur, (size_t)CS * 512);
                int sel = trial == 0 ? 0 : trial == 1 ? 1 : 2;
                for (int i = 0; i < b.np; ++i) { int take = sel == 0 ? 0 : sel == 1 ? 1 : (int)(crnd() & 1); if (take) memcpy(r.d + b.pend[i].lba * 512, b.pend[i].d, 512); }
                struct bigfs_dev d = mkdev(&r); runs++;
                fs = bigfs_mount(&d, &e);
                if (!fs) { bad++; if (barriers) printf("FALLO: no monta (caso %u corte %ld prueba %d)\n", c, cut, trial); free(r.d); continue; }
                spill_recover(fs, SP, TP);
                char k = classify(fs, SP, od, ol, "viejo.bin", nd, nl, "nuevo.bin");
                int ok = (k == 'n') || (!first && k == 'o') || (first && k == 'a');
                if (!ok) { bad++; if (barriers) printf("FALLO (con barreras): %s caso %u corte %ld prueba %d clase %c\n", first ? "primera" : "actualiza", c, cut, trial, k); }
                bigfs_unmount(fs); free(r.d);
            }
            b.budget = -1;
        }
        free(keep); free(b.live); free(b.dur); free(b.pend);
    }
    *runs_out = runs; return bad;
}

int main(void)
{
    static uint8_t od[60000], nd[60000];
    const char *SP = "/slot", *TP = "/tmp1";
    uint32_t sizes[][2] = { {20000, 30000}, {40000, 17000}, {17000, 17000}, {16384, 50000}, {30000, 0}, {0, 20000}, {5000, 5000} };
    for (int first = 0; first < 2; ++first)
    for (unsigned c = 0; c < sizeof sizes / sizeof sizes[0]; ++c) {
        uint32_t ol = sizes[c][0], nl = sizes[c][1]; fill(od, ol, 1); fill(nd, nl, 2);
        struct ram base = { calloc(4096, 512), 4096, 0, -1, 0 }; struct bigfs_dev d0 = mkdev(&base);
        CHECK(bigfs_format(&d0, 64) == 0, "format");
        int e; struct bigfs *fs = bigfs_mount(&d0, &e);
        if (!first) CHECK(spill_put_atomic(fs, SP, TP, "viejo.bin", 48, (const char *)od, ol) == 0, "put base");
        bigfs_unmount(fs);
        uint8_t *img = malloc(4096 * 512); memcpy(img, base.d, 4096 * 512);
        /* referencia: cuantas escrituras hace la actualizacion */
        long total;
        { struct ram r = { calloc(4096, 512), 4096, 0, -1, 0 }; memcpy(r.d, img, 4096 * 512); struct bigfs_dev d = mkdev(&r);
          fs = bigfs_mount(&d, &e); r.writes = 0; CHECK(spill_put_atomic(fs, SP, TP, "nuevo.bin", 48, (const char *)nd, nl) == 0, "put completo"); total = r.writes;
          CHECK(classify(fs, SP, od, ol, "viejo.bin", nd, nl, "nuevo.bin") == 'n', "completo => nuevo"); bigfs_unmount(fs); free(r.d); }
        for (long cut = 0; cut <= total; ++cut) {
            struct ram r = { calloc(4096, 512), 4096, 0, -1, 0 }; memcpy(r.d, img, 4096 * 512); struct bigfs_dev d = mkdev(&r);
            fs = bigfs_mount(&d, &e); r.writes = 0; r.crash_after = cut;
            spill_put_atomic(fs, SP, TP, "nuevo.bin", 48, (const char *)nd, nl);
            bigfs_unmount(fs); r.crash_after = -1;
            /* reinicio */
            fs = bigfs_mount(&d, &e); CHECK(fs != 0, "montaje tras corte");
            if (!fs) { free(r.d); continue; }
            spill_recover(fs, SP, TP);
            char k = classify(fs, SP, od, ol, "viejo.bin", nd, nl, "nuevo.bin");
            int ok = (k == 'n') || (!first && k == 'o') || (first && k == 'a');
            CHECK(ok, "%s caso %u (%u->%u) corte %ld/%ld: clase %c", first ? "primera vez" : "actualizacion", c, ol, nl, cut, total, k);
            if (!first) CHECK(k != 'a', "el archivo anterior se perdio (caso %u corte %ld)", c, cut);
            uint32_t t; CHECK(bigfs_lookup(fs, TP, &t) < 0, "temporal sin limpiar (caso %u corte %ld)", c, cut);
            /* segundo recover es no-op y el disco es consistente */
            CHECK(spill_recover(fs, SP, TP) == 0, "segundo recover");
            bigfs_unmount(fs);
            struct bigfs_fsck_report rep; CHECK(bigfs_fsck(&d, &rep) == 0 && rep.problems == 0, "fsck tras corte (caso %u corte %ld)", c, cut);
            /* y se puede volver a guardar */
            fs = bigfs_mount(&d, &e); CHECK(spill_put_atomic(fs, SP, TP, "otra.bin", 48, (const char *)nd, nl) == 0, "guardar tras recuperar"); bigfs_unmount(fs);
            free(r.d);
        }
        free(img); free(base.d);
    }
    /* disco lleno: la actualizacion falla pero el viejo queda intacto */
    { struct ram r = { calloc(200, 512), 200, 0, -1, 0 }; struct bigfs_dev d = mkdev(&r); int e;
      CHECK(bigfs_format(&d, 16) == 0, "format pequeno"); struct bigfs *fs = bigfs_mount(&d, &e);
      fill(od, 60000, 1); fill(nd, 60000, 2);
      int r1 = spill_put_atomic(fs, SP, TP, "a", 48, (const char *)od, 60000);
      int r2 = spill_put_atomic(fs, SP, TP, "b", 48, (const char *)nd, 60000);
      CHECK(r1 == 0, "primer put"); 
      if (r2 != 0) { CHECK(classify(fs, SP, od, 60000, "a", nd, 60000, "b") == 'o', "sin espacio: viejo intacto"); uint32_t t; CHECK(bigfs_lookup(fs, TP, &t) < 0, "temporal limpiado tras fallo"); }
      else { fails++; printf("ATENCION: el disco pequeno admitio ambos, la prueba de falta de espacio no se ejercito\n"); }
      bigfs_unmount(fs); free(r.d); }
    { long r1, r0; int b1 = cache_run(1, &r1); int b0 = cache_run(0, &r0);
      CHECK(b1 == 0, "con barreras: %d fallos de %ld cortes con cache volatil", b1, r1);
      CHECK(b0 > 0, "SIN barreras el modelo debia encontrar fallos (si no, no demuestra nada): %d de %ld", b0, r0);
      printf("cache volatil: con barreras %d fallos de %ld cortes; sin barreras %d fallos de %ld\n", b1, r1, b0, r0); }
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails ? 1 : 0;
}
