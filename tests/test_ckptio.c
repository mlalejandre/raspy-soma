/* PARCHE 120: pruebas exhaustivas del punto de control del VFS: corte tras cada escritura, cache volatil que reordena, corrupcion y mutaciones. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/fs/ckptio.h"

#define NSEC 2048
#define N 24u                  /* sectores de "VFS" de la prueba */
#define LIVE 100u
#define CK 400u
#define MK (CK - 1)
#define INT 50u
#define JR 60u
static uint8_t live[NSEC][512], dur[NSEC][512];
static struct { uint32_t lba; uint8_t d[512]; } pend[1024]; static int npend;
static long budget = -1; static int volatile_cache = 0; static int skip_flush = 0;
static uint64_t rs = 88172645463325252ull; static uint64_t rnd64(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static int rd(void *c, uint64_t l, void *b) { (void)c; if (l >= NSEC) return -1; memcpy(b, live[l], 512); return 0; }
static int wr(void *c, uint64_t l, const void *b) { (void)c; if (l >= NSEC || budget == 0) return -1; if (budget > 0) budget--;
    memcpy(live[l], b, 512); if (volatile_cache) { if (npend < 1024) { pend[npend].lba = (uint32_t)l; memcpy(pend[npend].d, b, 512); npend++; } } else memcpy(dur[l], b, 512); return 0; }
static int fl(void *c) { (void)c; if (skip_flush) return 0; for (int i = 0; i < npend; ++i) memcpy(dur[pend[i].lba], pend[i].d, 512); npend = 0; return 0; }
static const struct slotdev dev = { rd, wr, 0, fl };
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)

static void fillv(uint8_t v[N][512], uint8_t seed) { for (uint32_t s = 0; s < N; ++s) for (int i = 0; i < 512; ++i) v[s][i] = (uint8_t)(seed * 37 + s * 11 + i * 3 + (i >> 5)); }
static void put_live(uint8_t v[N][512]) { for (uint32_t s = 0; s < N; ++s) { memcpy(live[LIVE + s], v[s], 512); memcpy(dur[LIVE + s], v[s], 512); } }   /* estado previo ya durable */
static int live_is(uint8_t v[N][512]) { for (uint32_t s = 0; s < N; ++s) if (memcmp(dur[LIVE + s], v[s], 512)) return 0; return 1; }
static int ck_is(uint8_t v[N][512]) { for (uint32_t s = 0; s < N; ++s) if (memcmp(dur[CK + s], v[s], 512)) return 0; return 1; }
/* la imagen "tras el corte": lo durable pasa a ser lo visible */
static void crash(void) { memcpy(live, dur, sizeof live); npend = 0; }
/* corte con cache volatil: una parte aleatoria de lo no vaciado llega al disco, en cualquier orden, y parte se pierde */
static void crash_random(void) { for (int i = 0; i < npend; ++i) if (rnd64() & 1) memcpy(dur[pend[i].lba], pend[i].d, 512); memcpy(live, dur, sizeof live); npend = 0; }
static void clean(void) { memset(live, 0, sizeof live); memset(dur, 0, sizeof dur); npend = 0; budget = -1; volatile_cache = 0; skip_flush = 0; }
static uint8_t A[N][512], B[N][512];
#define SAVE(seq) ckpt_save(&dev, LIVE, CK, N, MK, seq)
#define VALID() ckpt_valid(&dev, CK, N, MK)
#define RESTORE() ckpt_restore(&dev, CK, LIVE, N, MK, INT, JR)
#define RECOVER() ckpt_recover(&dev, CK, LIVE, N, MK, INT, JR)

static void t_basico(void)
{
    clean(); fillv(A, 1); put_live(A);
    CHECK(VALID() == 0, "sin checkpoint no debe haberlo");
    CHECK(SAVE(1) == 0, "guardar"); CHECK(VALID() == 2, "valido con CRC"); CHECK(ck_is(A), "copia exacta");
    fillv(B, 2); put_live(B);
    CHECK(RESTORE() == 0, "restaurar"); CHECK(live_is(A), "el VFS vuelve al checkpoint"); CHECK(RECOVER() == 0, "sin pendientes");
    uint8_t m[512]; memcpy(m, dur[INT], 512); CHECK(m[0] == 0, "marca de intencion borrada");
}
static void t_corrupcion(void)
{
    clean(); fillv(A, 1); put_live(A); SAVE(1); fillv(B, 2); put_live(B);
    for (int pos = 0; pos < 512 * (int)N; pos += 997) { live[CK + pos / 512][pos % 512] ^= 0x10; dur[CK + pos / 512][pos % 512] ^= 0x10;
        CHECK(VALID() == 0, "un bit cambiado en el checkpoint (pos %d) debe invalidarlo", pos);
        CHECK(RESTORE() == -1, "restaurar un checkpoint corrupto debe negarse");
        live[CK + pos / 512][pos % 512] ^= 0x10; dur[CK + pos / 512][pos % 512] ^= 0x10; }
    CHECK(VALID() == 2, "de nuevo valido");
    uint8_t keep[N][512]; for (uint32_t s = 0; s < N; ++s) memcpy(keep[s], dur[LIVE + s], 512);
    dur[CK + 3][5] ^= 1; memcpy(live, dur, sizeof live);
    CHECK(RESTORE() == -1, "negado"); CHECK(live_is(B), "el VFS vivo NO se toca si el checkpoint esta corrupto");
    /* marcador con el CRC de cabecera roto */
    clean(); fillv(A, 1); put_live(A); SAVE(1); dur[MK][30] ^= 1; memcpy(live, dur, sizeof live); CHECK(VALID() == 0, "marcador alterado");
    /* formato antiguo V2 */
    clean(); fillv(A, 1); put_live(A); for (uint32_t s = 0; s < N; ++s) { memcpy(live[CK + s], A[s], 512); memcpy(dur[CK + s], A[s], 512); }
    memset(live[MK], 0, 512); memcpy(live[MK], CKPT_MAGIC2, 18); memcpy(dur[MK], live[MK], 512);
    CHECK(VALID() == 1, "V2 antiguo se acepta"); fillv(B, 2); put_live(B); memcpy(dur, live, sizeof dur); CHECK(RESTORE() == 0 && live_is(A), "restaurar V2");
}
/* corte tras CADA escritura del guardado: el marcador es invalido o el checkpoint es entero y nuevo */
static void t_guardar_cortes(int vol)
{
    long total = 0; int primer_valido = -1;
    for (long cut = 0; cut < 200; ++cut) {
        clean(); volatile_cache = vol; fillv(A, 1); put_live(A); memcpy(dur, live, sizeof live);
        if (SAVE(1) != 0) { CHECK(0, "guardado base"); return; } fl(0);
        fillv(B, 2); put_live(B);
        budget = cut; int r = SAVE(2); budget = -1;
        if (vol) crash_random(); else crash();
        int v = VALID();
        if (v != 0) { CHECK(v == 2, "tras el corte %ld: valido pero sin CRC", cut); CHECK(ck_is(B) || ck_is(A), "tras el corte %ld: checkpoint VALIDO pero a medias (ni el viejo ni el nuevo)", cut);
            if (ck_is(B) && primer_valido < 0) primer_valido = (int)cut;
            if (ck_is(A)) CHECK(cut <= 0 || vol, "tras el corte %ld: el checkpoint viejo sigue valido tras haber invalidado el marcador", cut); }
        total = cut;
        if (r == 0) break;
    }
    CHECK(primer_valido > 0, "el checkpoint nuevo no debe ser valido antes del final del guardado");
    printf("  guardado (%s): %ld cortes probados, valido solo con el guardado terminado (desde %d)\n", vol ? "cache volatil" : "sin cache", total + 1, primer_valido);
}
/* corte tras CADA escritura de la restauracion + recuperacion en el "arranque": VFS viejo o checkpoint completo, nunca mezcla */
static void t_restaurar_cortes(int vol)
{
    int viejos = 0, nuevos = 0, maxcut = 0;
    for (long cut = 0; cut < 300; ++cut) {
        clean(); volatile_cache = vol; fillv(A, 1); put_live(A); memcpy(dur, live, sizeof live); SAVE(1); fl(0);
        fillv(B, 2); put_live(B); fl(0); memcpy(dur, live, sizeof live);
        memset(live[JR], 0xAB, 512); memset(dur[JR], 0xAB, 512);   /* un "diario" pendiente que NO debe quedar vivo tras restaurar */
        budget = cut; int r = RESTORE(); budget = -1;
        if (vol) crash_random(); else crash();
        int rc = RECOVER();
        CHECK(rc >= 0, "recuperacion tras corte %ld fallo", cut);
        int es_a = live_is(A), es_b = live_is(B);
        CHECK(es_a || es_b, "tras corte %ld y recuperacion el VFS es una MEZCLA", cut);
        if (es_a) nuevos++; else viejos++;
        if (es_a) { CHECK(dur[JR][0] == 0, "tras restaurar, el diario viejo sigue vivo (corte %ld)", cut); }
        CHECK(RECOVER() == 0, "la recuperacion debe ser idempotente");
        maxcut = (int)cut; if (r == 0) break;
    }
    CHECK(viejos > 0 && nuevos > 0, "debe haber cortes en ambos lados (viejos %d, nuevos %d)", viejos, nuevos);
    printf("  restaurar (%s): %d cortes probados; %d dejan el VFS anterior, %d lo dejan restaurado completo\n", vol ? "cache volatil" : "sin cache", maxcut + 1, viejos, nuevos);
}
static void t_aleatorio(void)
{
    for (int it = 0; it < 3000; ++it) {
        clean(); volatile_cache = 1; fillv(A, (uint8_t)it); put_live(A); fl(0); fillv(B, (uint8_t)(it + 77)); (void)B;
        SAVE((uint32_t)it); fl(0); fillv(B, (uint8_t)(it + 5)); put_live(B); fl(0);
        budget = (long)(rnd64() % 90); int r = (rnd64() & 1) ? RESTORE() : SAVE(9); budget = -1; (void)r;
        crash_random(); RECOVER();
        int v = VALID();
        if (v) { uint8_t C[N][512]; for (uint32_t s = 0; s < N; ++s) memcpy(C[s], dur[CK + s], 512); CHECK(v == 2, "valido sin CRC"); CHECK(ck_is(A) || ck_is(B), "iteracion %d: checkpoint valido pero mezclado", it); (void)C; }
        CHECK(live_is(A) || live_is(B), "iteracion %d: VFS mezclado", it);
    }
}
/* mutaciones: si se quita una proteccion, el test DEBE fallar */
static int mutacion_sin_barrera_previa(void)
{
    int saved = fails; skip_flush = 1;
    int detected = 0;
    for (int it = 0; it < 400 && !detected; ++it) {
        clean(); volatile_cache = 1; skip_flush = 0; fillv(A, 1); put_live(A); fl(0); SAVE(1); fl(0); fillv(B, 2); put_live(B); fl(0);
        skip_flush = 1;                                               /* barrera inoperante */
        budget = (long)(rnd64() % 60); SAVE(2); budget = -1; skip_flush = 0;
        crash_random(); if (VALID() == 2 && !ck_is(B)) detected = 1; if (VALID() != 0 && !ck_is(B)) detected = 1;
    }
    fails = saved; return detected;
}
/* mutacion: sin la recuperacion del arranque, un corte a mitad de restaurar deja el VFS MEZCLADO (el test tiene que verlo) */
static int mutacion_sin_recuperar(void)
{
    for (long cut = 1; cut < 40; ++cut) {
        clean(); fillv(A, 1); put_live(A); SAVE(1); fillv(B, 2); put_live(B);
        budget = cut; RESTORE(); budget = -1; crash();
        if (!live_is(A) && !live_is(B)) return 1;
    }
    return 0;
}
int main(void)
{
    printf("== PARCHE 120: checkpoint del VFS ==\n");
    t_basico(); t_corrupcion();
    t_guardar_cortes(0); t_guardar_cortes(1); t_restaurar_cortes(0); t_restaurar_cortes(1); t_aleatorio();
    int m1 = mutacion_sin_barrera_previa();
    CHECK(m1, "MUTACION: sin barreras el test no detecto un checkpoint valido a medias");
    printf("  mutacion (sin barreras): %s\n", m1 ? "detectada" : "NO detectada");
    int m2 = mutacion_sin_recuperar(); CHECK(m2, "MUTACION: sin recuperacion no se vio ninguna mezcla");
    printf("  mutacion (sin recuperacion en el arranque): %s\n", m2 ? "detectada" : "NO detectada");
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails ? 1 : 0;
}
