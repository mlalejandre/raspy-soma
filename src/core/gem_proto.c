#include "gem_proto.h"
/* PARCHE 069 */
#define RX_USED  1u
#define RX_WRAP  2u
#define RX_SOF   0x00004000u
#define RX_EOF   0x00008000u
#define LEN_MASK 0x00001FFFu
#define TX_LAST  0x00008000u
#define TX_WRAP  0x40000000u
#define TX_USED  0x80000000u

uint32_t gem_nwcfg(int gigabit)
{
    return (gigabit ? (1u << 10) : 1u) | (1u << 1) | (1u << 8) | (1u << 17) | (3u << 18) | (2u << 21);   /* MDC /48 (3); bus de datos de 64 bits (2) */
}
struct gem_link gem_link_resolve(uint16_t bmsr, uint16_t anar, uint16_t lpa, uint16_t gbcr, uint16_t gbsr)
{
    struct gem_link l = { 0, 0, 0 };
    if (!(bmsr & BMSR_LINK)) return l;
    l.up = 1;
    uint16_t both = (uint16_t)(anar & lpa);
    if ((gbcr & GBCR_1000FULL) && (gbsr & GBSR_LP1000FULL)) { l.mbps = 1000; l.full = 1; }
    else if (both & 0x0100u) { l.mbps = 100; l.full = 1; }          /* 100BASE-TX FD */
    else if (both & 0x0080u) { l.mbps = 100; l.full = 0; }
    else if (both & 0x0040u) { l.mbps = 10;  l.full = 1; }
    else if (both & 0x0020u) { l.mbps = 10;  l.full = 0; }
    else { l.mbps = 10; l.full = 0; }                                 /* sin parte comun conocida: lo mas conservador */
    return l;
}
uint32_t gem_nwcfg_link(uint32_t mbps, int full)
{
    uint32_t v = gem_nwcfg(0) & ~(1u | (1u << 1) | (1u << 10));
    if (mbps == 1000) v |= (1u << 10); else if (mbps == 100) v |= 1u;
    if (full) v |= (1u << 1);
    return v;
}
uint32_t gem_dmacfg(uint32_t rxbuf)
{
    if (!rxbuf || rxbuf % 64u || rxbuf / 64u > 0xFFu) return 0;
    return (1u << 30) | ((rxbuf / 64u) << 16) | (1u << 10) | 16u;
}
uint32_t gem_mdio_cmd(int read, uint32_t phy, uint32_t reg, uint16_t data)
{
    return (1u << 30) | ((read ? 2u : 1u) << 28) | ((phy & 31u) << 23) | ((reg & 31u) << 18) | (2u << 16) | data;
}
void gem_mac_to_regs(const uint8_t m[6], uint32_t *lo, uint32_t *hi)
{
    *lo = (uint32_t)m[0] | ((uint32_t)m[1] << 8) | ((uint32_t)m[2] << 16) | ((uint32_t)m[3] << 24);
    *hi = (uint32_t)m[4] | ((uint32_t)m[5] << 8);
}
void gem_mac_from_regs(uint32_t lo, uint32_t hi, uint8_t m[6])
{
    m[0] = (uint8_t)lo; m[1] = (uint8_t)(lo >> 8); m[2] = (uint8_t)(lo >> 16); m[3] = (uint8_t)(lo >> 24); m[4] = (uint8_t)hi; m[5] = (uint8_t)(hi >> 8);
}
int gem_mac_valid(const uint8_t m[6])
{
    int zero = 1;
    for (int i = 0; i < 6; i++) if (m[i]) zero = 0;
    return !zero && !(m[0] & 1u);
}
void gem_rx_desc_init(uint32_t d[GEM_DESC_WORDS], uint64_t bus, int wrap)
{
    d[0] = ((uint32_t)bus & ~3u) | (wrap ? RX_WRAP : 0u); d[1] = 0; d[2] = (uint32_t)(bus >> 32); d[3] = 0;
}
void gem_rx_desc_release(uint32_t d[GEM_DESC_WORDS]) { d[1] = 0; d[0] &= ~RX_USED; }
int gem_rx_sw_owned(const uint32_t d[GEM_DESC_WORDS]) { return (d[0] & RX_USED) != 0; }
int gem_rx_whole(const uint32_t d[GEM_DESC_WORDS]) { return (d[1] & (RX_SOF | RX_EOF)) == (RX_SOF | RX_EOF); }
uint32_t gem_rx_len(const uint32_t d[GEM_DESC_WORDS]) { return d[1] & LEN_MASK; }
void gem_tx_desc_init(uint32_t d[GEM_DESC_WORDS], int wrap) { d[0] = 0; d[1] = TX_USED | (wrap ? TX_WRAP : 0u); d[2] = 0; d[3] = 0; }
int gem_tx_desc_fill(uint32_t d[GEM_DESC_WORDS], uint64_t bus, uint32_t len, int wrap)
{
    if (len == 0 || len > GEM_FRAME_MAX) return -1;
    d[0] = (uint32_t)bus; d[2] = (uint32_t)(bus >> 32); d[3] = 0;
    d[1] = len | TX_LAST | (wrap ? TX_WRAP : 0u);                    /* sin USED: del hardware */
    return 0;
}
int gem_tx_done(const uint32_t d[GEM_DESC_WORDS]) { return (d[1] & TX_USED) != 0; }
uint32_t gem_next(uint32_t i, uint32_t n) { return n ? (i + 1 >= n ? 0 : i + 1) : 0; }
