/* PARCHE 045: pruebas de la capa de bloque con dispositivos en RAM (ASAN+UBSAN). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/blk.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t *d0, *d1, *d2; static uint64_t s0 = 5000, s1 = 6000, s2 = 300; static int maxchunk_seen, fail_at = -1, calls;
static int rd(uint8_t *d, uint64_t lba, uint32_t n, void *buf) { if ((int)n > maxchunk_seen) maxchunk_seen = (int)n; if (n > BLK_CHUNK) return -1; if (++calls == fail_at) return -1; memcpy(buf, d + lba * 512, (size_t)n * 512); return 0; }
static int wr(uint8_t *d, uint64_t lba, uint32_t n, const void *buf) { if (n > BLK_CHUNK) return -1; memcpy(d + lba * 512, buf, (size_t)n * 512); return 0; }
static int r0(uint64_t l, uint32_t n, void *b) { return rd(d0, l, n, b); } static int w0(uint64_t l, uint32_t n, const void *b) { return wr(d0, l, n, b); }
static int r1(uint64_t l, uint32_t n, void *b) { return rd(d1, l, n, b); } static int w1(uint64_t l, uint32_t n, const void *b) { return wr(d1, l, n, b); }
static int r2(uint64_t l, uint32_t n, void *b) { return rd(d2, l, n, b); }

int main(void)
{
    d0 = malloc(s0 * 512); d1 = calloc(s1, 512); d2 = malloc(s2 * 512);
    for (uint64_t i = 0; i < s0 * 512; i++) d0[i] = (uint8_t)(i * 7 + (i >> 9));
    for (uint64_t i = 0; i < s2 * 512; i++) d2[i] = (uint8_t)(i * 3);
    blk_reset();
    struct blkdev a = { "vda", "virtio", 5000, r0, w0, 0 }, b = { "sda", "ahci", 6000, r1, w1, 1 }, c = { "nvme0", "nvme", 300, r2, 0, 1 };
    int ia = blk_register(&a), ib = blk_register(&b), ic = blk_register(&c);
    CHECK(ia == 0 && ib == 1 && ic == 2 && blk_count() == 3 && blk_find("sda") == 1 && blk_find("nope") == -1 && blk_find("sd") == -1);
    CHECK(blk_register(&b) == 1 && blk_count() == 3);                       /* re-registro: actualiza, no duplica */
    uint8_t *buf = malloc(1000 * 512);
    CHECK(blk_read(ia, 100, 1000, buf) == 0 && maxchunk_seen == BLK_CHUNK && !memcmp(buf, d0 + 100 * 512, 1000 * 512));   /* troceado */
    CHECK(blk_read(ia, 4999, 2, buf) == BLK_E_RANGE && blk_read(ia, 5000, 1, buf) == BLK_E_RANGE && blk_read(ia, 0, 0, buf) == BLK_E_ARG && blk_read(9, 0, 1, buf) == BLK_E_ARG);
    CHECK(blk_read(ia, 0xFFFFFFFFFFFFFF00ULL, 1, buf) == BLK_E_RANGE);
    CHECK(blk_write(ia, 0, 1, buf) == BLK_E_RO);                            /* disco del sistema: protegido */
    CHECK(blk_write(ic, 0, 1, buf) == BLK_E_RO);                            /* sin funcion de escritura */
    CHECK(blk_write(ib, 10, 300, d0) == 0 && !memcmp(d1 + 10 * 512, d0, 300 * 512));
    /* copia entre dispositivos con verificacion y troceado */
    CHECK(blk_copy(ia, 7, ib, 2000, 777, 1) == 0 && !memcmp(d1 + 2000 * 512, d0 + 7 * 512, 777 * 512));
    CHECK(blk_copy(ia, 0, ia, 100, 10, 0) == BLK_E_RO);
    CHECK(blk_copy(ib, 0, ib, 5, 10, 0) == BLK_E_ARG);                      /* solapado en el mismo dispositivo */
    CHECK(blk_copy(ib, 0, ib, 10, 10, 0) == 0);                             /* contiguo sin solape */
    CHECK(blk_copy(ia, 4990, ib, 0, 20, 0) == BLK_E_RANGE);
    CHECK(blk_copy(ia, 0, ib, 5990, 20, 0) == BLK_E_RANGE);
    fail_at = calls + 3; CHECK(blk_copy(ia, 0, ib, 3000, 400, 0) == BLK_E_IO); fail_at = -1;
    /* verificacion detecta corrupcion silenciosa */
    b.write = w0; blk_register(&b);                                          /* escribe en el dispositivo equivocado */
    CHECK(blk_copy(ia, 0, ib, 4000, 50, 1) == BLK_E_VERIFY);
    printf(fails ? "blk: %d FALLOS\n" : "blk: todo correcto\n", fails);
    free(d0); free(d1); free(d2); free(buf);
    return fails != 0;
}
