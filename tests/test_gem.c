#include <assert.h>
#include <stdio.h>
#include "../src/core/gem_proto.h"
int main(void)
{
    assert(gem_dmacfg(2048) == ((1u << 30) | (32u << 16) | (1u << 10) | 16u) && gem_dmacfg(0) == 0 && gem_dmacfg(100) == 0 && gem_dmacfg(64 * 256) == 0);
    assert((gem_nwcfg(1) & (1u << 10)) && !(gem_nwcfg(0) & (1u << 10)) && (gem_nwcfg(0) & 1u) && (gem_nwcfg(1) & 2u) && (gem_nwcfg(1) & (1u << 17)));
    /* MDIO: lectura de BMSR del PHY 1: ST=01, OP=10, PHYA=1, REGA=1, TA=10 */
    assert(gem_mdio_cmd(1, 1, 1, 0) == (0x40000000u | 0x20000000u | (1u << 23) | (1u << 18) | (2u << 16)));
    assert((gem_mdio_cmd(0, 31, 31, 0xABCD) & 0xFFFFu) == 0xABCDu && ((gem_mdio_cmd(0, 31, 31, 0) >> 28) & 3u) == 1u);
    {   /* PARCHE 073: resolucion de la negociacion automatica */
        struct gem_link l = gem_link_resolve(0x0000, ANAR_ALL, ANAR_ALL, 0, 0);
        assert(!l.up && l.mbps == 0);
        l = gem_link_resolve(BMSR_LINK, ANAR_ALL, 0x01E1, GBCR_1000FULL, GBSR_LP1000FULL);
        assert(l.up && l.mbps == 1000 && l.full);
        l = gem_link_resolve(BMSR_LINK, ANAR_ALL, 0x01E1, GBCR_1000FULL, 0);                 /* el otro extremo no ofrece 1000 */
        assert(l.mbps == 100 && l.full);
        l = gem_link_resolve(BMSR_LINK, ANAR_ALL, 0x00A1, 0, 0);                             /* 100 half + 10 half */
        assert(l.mbps == 100 && !l.full);
        l = gem_link_resolve(BMSR_LINK, ANAR_ALL, 0x0021, 0, 0);
        assert(l.mbps == 10 && !l.full);
        l = gem_link_resolve(BMSR_LINK, ANAR_ALL, 0x0041, 0, 0);
        assert(l.mbps == 10 && l.full);
        l = gem_link_resolve(BMSR_LINK, 0, 0, 0, 0);                                         /* sin parte comun */
        assert(l.up && l.mbps == 10 && !l.full);
        assert((gem_nwcfg_link(1000, 1) & (1u << 10)) && (gem_nwcfg_link(1000, 1) & 2u) && !(gem_nwcfg_link(1000, 1) & 1u));
        assert((gem_nwcfg_link(100, 0) & 1u) && !(gem_nwcfg_link(100, 0) & 2u) && !(gem_nwcfg_link(100, 0) & (1u << 10)));
        assert(!(gem_nwcfg_link(10, 1) & (1u << 10)) && !(gem_nwcfg_link(10, 1) & 1u) && (gem_nwcfg_link(10, 1) & 2u));
        assert((gem_nwcfg_link(10, 1) & (2u << 21)) == (2u << 21) && (gem_nwcfg_link(10, 1) & (1u << 17)));   /* conserva bus 64 bits y FCS */
    }
    uint8_t m[6] = { 0x02, 0x50, 0x49, 0x35, 0xAA, 0xBB }, m2[6]; uint32_t lo, hi;
    gem_mac_to_regs(m, &lo, &hi);
    assert(lo == 0x35495002u && hi == 0xBBAAu);
    gem_mac_from_regs(lo, hi, m2);
    for (int i = 0; i < 6; i++) assert(m[i] == m2[i]);
    uint8_t z[6] = { 0 }, mc[6] = { 1, 2, 3, 4, 5, 6 };
    assert(gem_mac_valid(m) && !gem_mac_valid(z) && !gem_mac_valid(mc));
    uint32_t d[4];
    gem_rx_desc_init(d, 0x1000345678ULL, 1);
    assert(d[0] == ((0x00345678u & ~3u) | 2u) && d[2] == 0x10 && !gem_rx_sw_owned(d));
    d[0] |= 1u; d[1] = 0xC000u | 60u;                                  /* el hardware recibe una trama de 60 bytes */
    assert(gem_rx_sw_owned(d) && gem_rx_whole(d) && gem_rx_len(d) == 60);
    gem_rx_desc_release(d);
    assert(!gem_rx_sw_owned(d) && (d[0] & 2u) && d[2] == 0x10 && d[1] == 0);
    d[1] = 0x4000u | 60u; assert(!gem_rx_whole(d));
    gem_tx_desc_init(d, 1);
    assert(gem_tx_done(d) && (d[1] & 0x40000000u));
    assert(gem_tx_desc_fill(d, 0x1000ABC000ULL, 64, 1) == 0 && !gem_tx_done(d) && d[0] == 0xABC000u && d[2] == 0x10 && (d[1] & 0x8000u) && (d[1] & 0x1FFFu) == 64);
    assert(gem_tx_desc_fill(d, 0, 0, 0) == -1 && gem_tx_desc_fill(d, 0, GEM_FRAME_MAX + 1, 0) == -1);
    assert(gem_next(0, 4) == 1 && gem_next(3, 4) == 0 && gem_next(0, 0) == 0);
    puts("gem_proto: todo correcto");
    return 0;
}
