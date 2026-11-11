#include "sd_proto.h"

uint32_t sd_cmd_word(uint8_t idx, int resp, int data)
{
    uint32_t w = (uint32_t)(idx & 0x3F) << 8;
    switch (resp) {
    case SD_R1:  case SD_R6: case SD_R7: w |= 2u | (1u << 3) | (1u << 4); break;     /* 48 bits, CRC e indice */
    case SD_R1B:                          w |= 3u | (1u << 3) | (1u << 4); break;     /* 48 bits con "busy" */
    case SD_R2:                           w |= 1u | (1u << 3); break;                 /* 136 bits, CRC */
    case SD_R3:                           w |= 2u; break;                             /* 48 bits sin CRC ni indice */
    default: break;
    }
    if (data) w |= 1u << 5;
    return w;
}

uint32_t sd_clk_div(uint32_t base_hz, uint32_t target_hz)
{
    if (target_hz == 0 || base_hz <= target_hz) return 0;
    uint64_t n = ((uint64_t)base_hz + 2ULL * target_hz - 1) / (2ULL * target_hz);
    return n > 1023 ? 1023u : (uint32_t)n;
}

uint32_t sd_clock_reg(uint32_t n) { return ((n & 0xFFu) << 8) | (((n >> 8) & 3u) << 6) | 1u; }

static uint32_t bits(const uint32_t r[4], int hi, int lo)       /* campo del CSD completo; r[] contiene los bits 127:8 desplazados 8 */
{
    hi -= 8; lo -= 8;
    uint32_t v = 0;
    for (int b = hi; b >= lo; b--) v = (v << 1) | ((r[b / 32] >> (b % 32)) & 1u);
    return v;
}

int sd_csd_sectors(const uint32_t r[4], uint64_t *sectors)
{
    uint32_t ver = bits(r, 127, 126);
    if (ver == 1) { *sectors = ((uint64_t)bits(r, 69, 48) + 1) * 1024; return 0; }       /* CSD v2 (SDHC / SDXC) */
    if (ver == 0) {                                                                       /* CSD v1 */
        uint32_t bl = bits(r, 83, 80), cs = bits(r, 73, 62), mult = bits(r, 49, 47);
        if (bl < 9 || bl > 11) return -1;
        *sectors = ((uint64_t)(cs + 1) << (mult + 2)) << (bl - 9);
        return 0;
    }
    return -1;
}
