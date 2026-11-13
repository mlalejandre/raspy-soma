/* PARCHE 112: prueba exhaustiva de caidas del diario del VFS. Se simula un corte tras CADA escritura posible. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/fs/slotio.h"

#define NSEC 4096
#define JL 100u
#define BASE 200u
#define SPF 33u
#define NSLOT 4u
static uint8_t disk[NSEC][512];
static long budget = -1, nwrites;
static int rd(void *c, uint64_t l, void *b) { (void)c; if (l >= NSEC) return -1; memcpy(b, disk[l], 512); return 0; }
static int wr(void *c, uint64_t l, const void *b) { (void)c; if (l >= NSEC) return -1; if (budget == 0) return -1; if (budget > 0) budget--; memcpy(disk[l], b, 512); nwrites++; return 0; }
static const struct slotdev dev = { rd, wr, 0, 0 };
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)

static void mkhdr(uint8_t h[512], uint32_t size, uint8_t tag) { memset(h, 0, 512); memcpy(h, "file", 4); h[4] = tag; memcpy(h + 48, &size, 4); h[52] = 1; }
static void mkdata(uint8_t *d, uint32_t n, uint8_t seed) { for (uint32_t i = 0; i < n; ++i) d[i] = (uint8_t)(seed * 31 + i * 7 + (i >> 8)); }

/* Lee el inodo del disco y devuelve 'o' si es entero el viejo, 'n' si es entero el nuevo, '?' si esta a medias. */
static char classify(uint32_t slot, const uint8_t *od, uint32_t os, uint8_t otag, const uint8_t *nd, uint32_t ns, uint8_t ntag)
{
    uint64_t b = BASE + (uint64_t)slot * SPF; uint8_t h[512], eh[512], sec[512];
    memcpy(h, disk[b], 512);
    for (int pass = 0; pass < 2; ++pass) {
        const uint8_t *d = pass ? nd : od; uint32_t sz = pass ? ns : os; uint8_t tg = pass ? ntag : otag;
        mkhdr(eh, sz, tg);
        if (memcmp(h, eh, 512)) continue;
        int ok = 1;
        for (uint32_t s = 0; s < 32; ++s) { slotio_data_sector(d, sz, s, sec); if (memcmp(sec, disk[b + 1 + s], 512)) { ok = 0; break; } }
        if (ok) return pass ? 'n' : 'o';
    }
    return '?';
}


/* ---- PARCHE 115: modelo de disco con CACHE VOLATIL que puede reordenar y perder escrituras no vaciadas (flush) ---- */
static uint8_t live[NSEC][512], dur[NSEC][512];
static struct { uint32_t lba; uint8_t d[512]; } pend[512];
static int npend; static long cbudget;
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd64(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static int c_rd(void *c, uint64_t l, void *b) { (void)c; if (l >= NSEC) return -1; memcpy(b, live[l], 512); return 0; }
static int c_wr(void *c, uint64_t l, const void *b) { (void)c; if (l >= NSEC || cbudget == 0) return -1; if (cbudget > 0) cbudget--;
    memcpy(live[l], b, 512); if (npend < 512) { pend[npend].lba = (uint32_t)l; memcpy(pend[npend].d, b, 512); npend++; } else { fails++; printf("modelo: cola llena\n"); } return 0; }
static int c_fl(void *c) { (void)c; for (int i = 0; i < npend; ++i) memcpy(dur[pend[i].lba], pend[i].d, 512); npend = 0; return 0; }
static void c_sync(void) { c_fl(0); memcpy(live, dur, sizeof live); }

/* Devuelve el numero de corridas con inodo a medias. */
static int cache_run(int barriers, long *runs_out)
{
    struct slotdev cdev = { c_rd, c_wr, 0, barriers ? c_fl : 0 };
    static uint8_t od[16384], nd[16384]; int bad = 0; long runs = 0;
    uint32_t sizes[][2] = { {1000, 5000}, {16000, 100}, {0, 700}, {5000, 16383}, {4096, 4096} };
    for (unsigned c = 0; c < sizeof sizes / sizeof sizes[0]; ++c) {
        uint32_t os = sizes[c][0], ns = sizes[c][1]; mkdata(od, os, 1); mkdata(nd, ns, 2);
        uint8_t oh[512], nh[512]; mkhdr(oh, os, 'O'); mkhdr(nh, ns, 'N');
        long total;
        memset(dur, 0, sizeof dur); memset(live, 0, sizeof live); npend = 0; cbudget = -1;
        slotio_commit(&cdev, JL, BASE + 1 * SPF, 1, oh, od, os, 1); c_sync();
        { static uint8_t keep[NSEC][512]; memcpy(keep, dur, sizeof keep);
          long w = 0; cbudget = 1000000; slotio_commit(&cdev, JL, BASE + 1 * SPF, 1, nh, nd, ns, 2); w = 1000000 - cbudget; total = w;
          for (long cut = 0; cut <= total; ++cut) {
            for (int trial = 0; trial < 14; ++trial) {
                memcpy(dur, keep, sizeof dur); memcpy(live, keep, sizeof live); npend = 0; cbudget = cut;
                slotio_commit(&cdev, JL, BASE + 1 * SPF, 1, nh, nd, ns, 2); cbudget = -1;
                /* corte de luz: lo durable + un subconjunto arbitrario (y en cualquier orden de llegada) de lo pendiente */
                memcpy(disk, dur, sizeof disk);
                int sel = trial == 0 ? 0 : trial == 1 ? 1 : 2;
                for (int i = 0; i < npend; ++i) { int take = sel == 0 ? 0 : sel == 1 ? 1 : (int)(rnd64() & 1); if (take) memcpy(disk[pend[i].lba], pend[i].d, 512); }
                budget = -1;
                slotio_recover(&dev, JL, BASE, SPF, NSLOT);
                char k = classify(1, od, os, 'O', nd, ns, 'N'); runs++;
                if (k != 'o' && k != 'n') { bad++; if (barriers) printf("FALLO (con barreras): caso %u corte %ld prueba %d: clase %c\n", c, cut, trial, k); }
            }
          } }
    }
    *runs_out = runs; return bad;
}

int main(void)
{
    static uint8_t od[16384], nd[16384];
    uint32_t sizes[][2] = { {1000, 5000}, {16000, 100}, {0, 700}, {512, 512}, {16383, 16383}, {5000, 0}, {1, 16384} };
    for (unsigned c = 0; c < sizeof sizes / sizeof sizes[0]; ++c) {
        uint32_t os = sizes[c][0], ns = sizes[c][1];
        mkdata(od, os, 1); mkdata(nd, ns, 2);
        uint8_t oh[512], nh[512];
        mkhdr(oh, os, 'O'); mkhdr(nh, ns, 'N');
        /* medir cuantas escrituras hace una actualizacion completa */
        memset(disk, 0, sizeof disk);
        budget = -1; CHECK(slotio_commit(&dev, JL, BASE + 1 * SPF, 1, oh, od, os, 1) == 0, "commit base");
        nwrites = 0; CHECK(slotio_commit(&dev, JL, BASE + 1 * SPF, 1, nh, nd, ns, 2) == 0, "commit completo"); long total = nwrites;
        CHECK(classify(1, od, os, 'O', nd, ns, 'N') == 'n', "completo => nuevo (caso %u)", c);
        for (long cut = 0; cut <= total; ++cut) {
            /* estado base: inodo viejo entero, otros inodos con marca */
            memset(disk, 0, sizeof disk);
            budget = -1; slotio_commit(&dev, JL, BASE + 1 * SPF, 1, oh, od, os, 1);
            for (int k = 0; k < 33; ++k) disk[BASE + 3 * SPF + k][0] = 0xAB;      /* vecino que no debe tocarse */
            budget = cut; int r = slotio_commit(&dev, JL, BASE + 1 * SPF, 1, nh, nd, ns, 2); budget = -1;
            CHECK((cut < total) == (r != 0), "valor de retorno caso %u corte %ld", c, cut);
            /* "reinicio": recuperar */
            int rc = slotio_recover(&dev, JL, BASE, SPF, NSLOT);
            CHECK(rc >= 0, "recover error caso %u corte %ld", c, cut);
            char k = classify(1, od, os, 'O', nd, ns, 'N');
            CHECK(k == 'o' || k == 'n', "INODO A MEDIAS caso %u (%u->%u bytes) corte %ld de %ld (clase %c)", c, os, ns, cut, total, k);
            /* monotonia: si el compromiso ya se hizo, debe quedar el nuevo */
            long commit_at = (long)slotio_nd(ns) + 1;       /* escrituras previas al sector de compromiso (datos + cabecera) */
            if (cut > commit_at) CHECK(k == 'n', "tras compromiso debe ser nuevo caso %u corte %ld", c, cut);
            if (cut <= commit_at) CHECK(k == 'o', "antes del compromiso debe ser viejo caso %u corte %ld", c, cut);
            uint8_t z[512]; memset(z, 0, 512);
            CHECK(memcmp(disk[JL], z, 512) == 0, "diario no invalidado caso %u corte %ld", c, cut);
            for (int q = 0; q < 33; ++q) CHECK(disk[BASE + 3 * SPF + q][0] == 0xAB, "vecino pisado caso %u corte %ld", c, cut);
            CHECK(slotio_recover(&dev, JL, BASE, SPF, NSLOT) == 0, "segundo recover debe ser no-op");
            /* idempotencia ante caidas DURANTE la recuperacion */
        }
        /* caida durante la propia recuperacion: comprometer y cortar en cada escritura de recover */
        for (long cut = 0; cut < 40; ++cut) {
            memset(disk, 0, sizeof disk); budget = -1; slotio_commit(&dev, JL, BASE + 1 * SPF, 1, oh, od, os, 1);
            long ca = (long)slotio_nd(ns) + 2;           /* justo despues del compromiso: diario valido, inodo sin tocar */
            budget = ca; slotio_commit(&dev, JL, BASE + 1 * SPF, 1, nh, nd, ns, 2);
            budget = cut; slotio_recover(&dev, JL, BASE, SPF, NSLOT);   /* puede cortarse */
            budget = -1; slotio_recover(&dev, JL, BASE, SPF, NSLOT);    /* segundo arranque completa */
            CHECK(classify(1, od, os, 'O', nd, ns, 'N') == 'n', "recuperacion interrumpida caso %u corte %ld", c, cut);
        }
    }
    /* diario corrupto (bits volteados) => se descarta, el inodo no se toca */
    for (int flip = 0; flip < 64; ++flip) {
        uint32_t os = 3000, ns = 9000; mkdata(od, os, 1); mkdata(nd, ns, 2);
        uint8_t oh[512], nh[512]; mkhdr(oh, os, 'O'); mkhdr(nh, ns, 'N');
        memset(disk, 0, sizeof disk); budget = -1; slotio_commit(&dev, JL, BASE + 1 * SPF, 1, oh, od, os, 1);
        budget = (long)slotio_nd(ns) + 2; slotio_commit(&dev, JL, BASE + 1 * SPF, 1, nh, nd, ns, 2); budget = -1;   /* diario comprometido, sin aplicar */
        disk[JL + 2 + (flip % 18)][(flip * 37) % 512] ^= (uint8_t)(1u << (flip % 8));
        int rc = slotio_recover(&dev, JL, BASE, SPF, NSLOT);
        CHECK(rc == 2, "diario corrupto debe descartarse (rc=%d)", rc);
        CHECK(classify(1, od, os, 'O', nd, ns, 'N') == 'o', "diario corrupto no debe alterar el inodo");
    }
    /* diario con indice de ranura fuera de rango => descartar */
    { uint8_t oh[512], nh[512]; mkhdr(oh, 10, 'O'); mkhdr(nh, 20, 'N'); mkdata(od, 10, 1); mkdata(nd, 20, 2);
      memset(disk, 0, sizeof disk); budget = 5; slotio_commit(&dev, JL, BASE + 9 * SPF, 9, nh, nd, 20, 1); budget = -1;
      CHECK(slotio_recover(&dev, JL, BASE, SPF, NSLOT) == 2, "ranura fuera de rango"); }
    /* tamano que no cabe */
    CHECK(slotio_commit(&dev, JL, BASE, 0, (uint8_t[512]){0}, od, 16385, 1) == -1, "tamano excesivo");
    /* PARCHE 115: con cache volatil y reordenamiento, el diario necesita barreras; el modelo debe pasar con ellas y fallar sin ellas */
    { long r1, r0; int b1 = cache_run(1, &r1); int b0 = cache_run(0, &r0);
      CHECK(b1 == 0, "con barreras: %d inodos a medias de %ld cortes con cache", b1, r1);
      CHECK(b0 > 0, "SIN barreras el modelo debia encontrar inodos a medias (si no, el modelo no demuestra nada): %d de %ld", b0, r0);
      printf("cache volatil: con barreras %d fallos de %ld cortes; sin barreras %d fallos de %ld\n", b1, r1, b0, r0); }
    /* CRC32 de referencia */
    CHECK(slotio_crc32(0, "123456789", 9) == 0xCBF43926u, "crc32 de referencia");
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails ? 1 : 0;
}
