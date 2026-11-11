#include "gem.h"
#include "rp1.h"
#include "rp1_proto.h"
#include "gem_proto.h"
#include "pmm.h"
#include "rpi_fb.h"
#include "platform.h"
#include "console.h"
#include "idt.h"

/* PARCHE 069: driver de sondeo (sin interrupciones) de la GEM del RP1. Anillos de 16 (RX) y 8 (TX) descriptores de 64 bits en una zona
 * contigua; los dispositivos ven la RAM en SOMA_PCIE_DMA_PCI + direccion fisica. Cache: se limpia/invalida alrededor de cada acceso
 * (necesario en hardware no coherente). Parche 073: reinicio del PHY por MDIO y negociacion automatica (velocidad y duplex resueltos en gem_proto).
 * Pendiente en placa real: reloj/modo RGMII del RP1 por velocidad y reset del PHY por GPIO. */
#define RXN 16u
#define TXN 8u
#define DMA_FRAMES 14u

static int g_state;
static volatile uint32_t *R;
static uint32_t *rxd, *txd;
static uint8_t *rxb, *txb;
static uint32_t rxi, txi;
static uint8_t g_mac[6] = { 0x02, 0x50, 0x49, 0x35, 0x00, 0x01 };
static uint32_t st_tx, st_rx; static uint64_t st_txb, st_rxb;
static int g_link;
static int g_phy = -1;                 /* PARCHE 073 */
static uint32_t g_mbps; static int g_full; static uint64_t g_last_chk;

static inline uint32_t rd(uint32_t off) { return R[off / 4]; }
static inline void wr(uint32_t off, uint32_t v) { R[off / 4] = v; __asm__ volatile ("dsb sy" ::: "memory"); }
static inline uint64_t busaddr(const void *p) { return (uint64_t)(uintptr_t)p + SOMA_PCIE_DMA_PCI; }

static int mdio_wait(void)
{
    uint64_t t0 = timer_ticks;
    while (!(rd(GEM_NWSTATUS) & GEM_NWSTATUS_MDIO_IDLE)) if (timer_ticks - t0 > 20) return -1;
    return 0;
}
static int mdio_read(uint32_t phy, uint32_t reg, uint16_t *v)
{
    if (mdio_wait()) return -1;
    wr(GEM_PHYMNTNC, gem_mdio_cmd(1, phy, reg, 0));
    if (mdio_wait()) return -1;
    *v = (uint16_t)rd(GEM_PHYMNTNC);
    return 0;
}

static int mdio_write(uint32_t phy, uint32_t reg, uint16_t v)
{
    if (mdio_wait()) return -1;
    wr(GEM_PHYMNTNC, gem_mdio_cmd(0, phy, reg, v));
    return mdio_wait();
}

/* PARCHE 073: lee el PHY, resuelve velocidad/duplex y reprograma la MAC. Devuelve 1 si hay enlace. */
static int link_refresh(void)
{
    uint16_t bmsr = 0, anar = 0, lpa = 0, gbcr = 0, gbsr = 0;
    if (g_phy < 0) return 0;
    if (mdio_read((uint32_t)g_phy, MII_BMSR, &bmsr) || mdio_read((uint32_t)g_phy, MII_BMSR, &bmsr)) return 0;   /* el bit de enlace es "latch low": dos lecturas */
    mdio_read((uint32_t)g_phy, MII_ANAR, &anar); mdio_read((uint32_t)g_phy, MII_ANLPAR, &lpa);
    mdio_read((uint32_t)g_phy, MII_GBCR, &gbcr); mdio_read((uint32_t)g_phy, MII_GBSR, &gbsr);
    struct gem_link l = gem_link_resolve(bmsr, anar, lpa, gbcr, gbsr);
    if (l.up && (l.mbps != g_mbps || l.full != g_full)) {
        wr(GEM_NWCFG, gem_nwcfg_link(l.mbps, l.full));
        g_mbps = l.mbps; g_full = l.full;
    }
    if (!l.up) { g_mbps = 0; }
    g_link = l.up;
    return l.up;
}

static void phy_start(void)
{
    uint16_t v;
    mdio_write((uint32_t)g_phy, MII_BMCR, BMCR_RESET);                                  /* reinicio por software del PHY */
    uint64_t t0 = timer_ticks;
    while (timer_ticks - t0 < 500) { if (mdio_read((uint32_t)g_phy, MII_BMCR, &v) == 0 && !(v & BMCR_RESET)) break; }
    mdio_write((uint32_t)g_phy, MII_ANAR, ANAR_ALL);
    mdio_write((uint32_t)g_phy, MII_GBCR, GBCR_1000FULL);
    mdio_write((uint32_t)g_phy, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);              /* negociacion automatica */
    t0 = timer_ticks;
    while (timer_ticks - t0 < 1500) {                                                     /* espera acotada: el enlace puede tardar mas y se re-evalua despues */
        if (mdio_read((uint32_t)g_phy, MII_BMSR, &v) == 0 && (v & BMSR_ANDONE)) break;
    }
    link_refresh();
}

static void u8out(uint32_t v) { char c[4]; int n = 0; if (!v) c[n++] = '0'; while (v) { c[n++] = (char)('0' + v % 10); v /= 10; } char o[5]; int k = 0; while (n) o[k++] = c[--n]; o[k] = 0; kprint(o); }

int gem_init(void)
{
    if (g_state) return g_state > 0;
    g_state = -1;
    if (rp1_init() != 0) return 0;
    uint64_t b = rp1_periph_base(RP1_ETH);
    if (!b) return 0;
    R = (volatile uint32_t *)(uintptr_t)b;
    uint32_t modid = rd(0xFC);
    if (modid == 0 || modid == 0xFFFFFFFFu) return 0;                              /* no hay MAC ahi */
    wr(GEM_NWCTRL, 0); wr(GEM_IDR, 0xFFFFFFFFu);
    uintptr_t m = pmm_alloc_dma(DMA_FRAMES);
    if (!m) return 0;
    uint8_t *z = (uint8_t *)m;
    for (uint32_t i = 0; i < DMA_FRAMES * 4096u; i++) z[i] = 0;
    rxd = (uint32_t *)z;                           /* 256 B */
    txd = (uint32_t *)(z + 256);                   /* 128 B */
    rxb = z + 4096;                                /* 16 x 2048 */
    txb = rxb + RXN * GEM_RXBUF_SIZE;              /* 8 x 2048 */
    for (uint32_t i = 0; i < RXN; i++) gem_rx_desc_init(&rxd[i * GEM_DESC_WORDS], busaddr(rxb + i * GEM_RXBUF_SIZE), i == RXN - 1);
    for (uint32_t i = 0; i < TXN; i++) gem_tx_desc_init(&txd[i * GEM_DESC_WORDS], i == TXN - 1);
    soma_dcache_clean(z, DMA_FRAMES * 4096u);
    rxi = txi = 0;
    wr(GEM_NWCFG, gem_nwcfg(1));
    wr(GEM_DMACFG, gem_dmacfg(GEM_RXBUF_SIZE));
    uint64_t rb = busaddr(rxd), tb = busaddr(txd);
    wr(GEM_RXQBASE, (uint32_t)rb); wr(GEM_RBQPH, (uint32_t)(rb >> 32));
    wr(GEM_TXQBASE, (uint32_t)tb); wr(GEM_TBQPH, (uint32_t)(tb >> 32));
    uint8_t mac[6]; gem_mac_from_regs(rd(GEM_SPADDR1LO), rd(GEM_SPADDR1HI), mac);
    if (gem_mac_valid(mac)) { for (int i = 0; i < 6; i++) g_mac[i] = mac[i]; }
    { uint32_t lo, hi; gem_mac_to_regs(g_mac, &lo, &hi); wr(GEM_SPADDR1LO, lo); wr(GEM_SPADDR1HI, hi); }   /* escribir HI activa el filtro de direccion */
    wr(GEM_RXSTATUS, 0xFu); wr(GEM_TXSTATUS, 0xFFu);
    wr(GEM_NWCTRL, GEM_NWCTRL_MDIO | GEM_NWCTRL_RXEN | GEM_NWCTRL_TXEN);
    int phy_found = -1;
    for (uint32_t p = 0; p < 32 && phy_found < 0; p++) {
        uint16_t v;
        if (mdio_read(p, 1, &v) == 0 && v != 0xFFFF && v != 0) { phy_found = (int)p; g_link = (v >> 2) & 1; }
    }
    g_state = 1;
    g_phy = phy_found;
    if (phy_found >= 0) phy_start();
    kprint("GEM: MAC Ethernet del RP1 activa (PHY ");
    if (phy_found < 0) kprint("sin respuesta");
    else { u8out((uint32_t)phy_found); kprint(g_link ? ", enlace arriba " : ", enlace abajo"); if (g_link) { u8out(g_mbps); kprint(g_full ? " Mb/s duplex completo" : " Mb/s semiduplex"); } }
    kprint(")\n");
    return 1;
}

const uint8_t *gem_mac(void) { return g_mac; }
int gem_present(void) { return g_state > 0; }
int gem_link_up(void)
{
    if (g_state <= 0) return 0;
    if (g_phy >= 0 && timer_ticks - g_last_chk >= 1000) { g_last_chk = timer_ticks; link_refresh(); }   /* PARCHE 073: reevalua cada segundo (cable conectado/desconectado) */
    return g_link;
}
uint32_t gem_speed_mbps(void) { return g_link ? g_mbps : 0; }
int gem_full_duplex(void) { return g_full; }

int gem_send(const void *frame, uint16_t len)
{
    if (g_state <= 0 || !frame || len == 0 || len > GEM_FRAME_MAX) return -1;
    uint32_t *d = &txd[txi * GEM_DESC_WORDS];
    uint64_t t0 = timer_ticks;
    for (;;) {
        soma_dcache_flush_inval(d, GEM_DESC_WORDS * 4);
        if (gem_tx_done(d)) break;
        if (timer_ticks - t0 > 50) return -1;                                      /* el anillo sigue lleno */
    }
    uint8_t *buf = txb + txi * GEM_RXBUF_SIZE;
    const uint8_t *src = (const uint8_t *)frame;
    uint32_t n = len < 60 ? 60u : len;
    for (uint32_t i = 0; i < n; i++) buf[i] = i < len ? src[i] : 0;
    soma_dcache_clean(buf, n);
    if (gem_tx_desc_fill(d, busaddr(buf), n, txi == TXN - 1) != 0) return -1;
    soma_dcache_clean(d, GEM_DESC_WORDS * 4);
    txi = gem_next(txi, TXN);
    wr(GEM_NWCTRL, GEM_NWCTRL_MDIO | GEM_NWCTRL_RXEN | GEM_NWCTRL_TXEN | GEM_NWCTRL_TSTART);
    st_tx++; st_txb += n;
    return 0;
}

int gem_poll(void *out, uint16_t max, uint16_t *len_out)
{
    if (g_state <= 0) return 0;
    for (uint32_t tries = 0; tries < RXN; tries++) {
        uint32_t *d = &rxd[rxi * GEM_DESC_WORDS];
        soma_dcache_flush_inval(d, GEM_DESC_WORDS * 4);
        if (!gem_rx_sw_owned(d)) return 0;
        uint32_t len = gem_rx_len(d);
        int ok = gem_rx_whole(d) && len >= 14 && len <= max;
        if (ok) {
            uint8_t *buf = rxb + rxi * GEM_RXBUF_SIZE;
            soma_dcache_flush_inval(buf, len);
            for (uint32_t i = 0; i < len; i++) ((uint8_t *)out)[i] = buf[i];
            *len_out = (uint16_t)len;
            st_rx++; st_rxb += len;
        }
        gem_rx_desc_release(d);
        soma_dcache_clean(d, GEM_DESC_WORDS * 4);
        rxi = gem_next(rxi, RXN);
        if (rd(GEM_RXSTATUS) & 1u) wr(GEM_RXSTATUS, 1u);                           /* buffer no disponible: se limpia al devolver descriptores */
        if (ok) return 1;
    }
    return 0;
}

void gem_stats(uint32_t *tx_pkts, uint32_t *rx_pkts, uint64_t *tx_b, uint64_t *rx_b)
{
    if (tx_pkts) *tx_pkts = st_tx;
    if (rx_pkts) *rx_pkts = st_rx;
    if (tx_b) *tx_b = st_txb;
    if (rx_b) *rx_b = st_rxb;
}
