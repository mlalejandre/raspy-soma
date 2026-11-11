/* PARCHE 061: pruebas de sd_proto (palabras de orden, divisor de reloj, capacidad desde el CSD). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/core/sd_proto.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* Construye un CSD completo (128 bits) con unos campos y devuelve la respuesta R2 de SDHCI (CRC fuera). */
static void put(uint8_t csd[16], int hi, int lo, uint32_t v) { for (int b = lo; b <= hi; b++) { int bit = (v >> (b - lo)) & 1; if (bit) csd[b / 8] |= (uint8_t)(1u << (b % 8)); } }
static void to_r2(const uint8_t csd[16], uint32_t r[4]) { for (int i = 0; i < 4; i++) { r[i] = 0; for (int k = 0; k < 4; k++) { int ix = 1 + i * 4 + k; if (ix < 16) r[i] |= (uint32_t)csd[ix] << (8 * k); } } }

int main(void)
{
    CHECK(sd_cmd_word(0, SD_R_NONE, 0) == 0);
    CHECK(sd_cmd_word(8, SD_R7, 0) == ((8u << 8) | 2 | 8 | 16));
    CHECK(sd_cmd_word(41, SD_R3, 0) == ((41u << 8) | 2));
    CHECK(sd_cmd_word(2, SD_R2, 0) == ((2u << 8) | 1 | 8));
    CHECK(sd_cmd_word(7, SD_R1B, 0) == ((7u << 8) | 3 | 8 | 16));
    CHECK(sd_cmd_word(18, SD_R1, 1) == ((18u << 8) | 2 | 8 | 16 | 32));
    CHECK(sd_clk_div(200000000, 400000) == 250);
    CHECK(sd_clk_div(200000000, 25000000) == 4);
    CHECK(sd_clk_div(100000000, 100000000) == 0 && sd_clk_div(50, 100) == 0);
    CHECK(sd_clk_div(200000000, 1) == 1023 && sd_clk_div(1, 0) == 0);
    CHECK(sd_clock_reg(250) == ((250u << 8) | 1) && sd_clock_reg(0x2FF) == ((0xFFu << 8) | (2u << 6) | 1));
    uint8_t csd[16]; uint32_t r[4]; uint64_t s;
    memset(csd, 0, 16); put(csd, 127, 126, 1); put(csd, 69, 48, 0x1DFF);                 /* v2: (c_size+1)*1024 sectores */
    to_r2(csd, r); CHECK(sd_csd_sectors(r, &s) == 0 && s == (0x1DFFull + 1) * 1024);
    memset(csd, 0, 16); put(csd, 127, 126, 1); put(csd, 69, 48, 0x3FFFFF);
    to_r2(csd, r); CHECK(sd_csd_sectors(r, &s) == 0 && s == 0x400000ull * 1024);
    memset(csd, 0, 16); put(csd, 127, 126, 0); put(csd, 83, 80, 10); put(csd, 73, 62, 0xFFF); put(csd, 49, 47, 7);   /* v1: 1024 B * 4096 * 512 = 2 GiB */
    to_r2(csd, r); CHECK(sd_csd_sectors(r, &s) == 0 && s == 4194304ull);
    memset(csd, 0, 16); put(csd, 127, 126, 0); put(csd, 83, 80, 9); put(csd, 73, 62, 1023); put(csd, 49, 47, 2);       /* 1024 * 16 * 512 B = 8 MiB */
    to_r2(csd, r); CHECK(sd_csd_sectors(r, &s) == 0 && s == 16384ull);
    memset(csd, 0, 16); put(csd, 127, 126, 0); put(csd, 83, 80, 3);
    to_r2(csd, r); CHECK(sd_csd_sectors(r, &s) == -1);
    memset(csd, 0, 16); put(csd, 127, 126, 2);
    to_r2(csd, r); CHECK(sd_csd_sectors(r, &s) == -1);
    srand(7);
    for (int i = 0; i < 20000; i++) { for (int k = 0; k < 4; k++) r[k] = (uint32_t)rand() * 2654435761u; sd_csd_sectors(r, &s); }
    if (fails) { printf("sd_proto: %d fallos\n", fails); return 1; }
    printf("sd_proto: todo correcto\n");
    return 0;
}
