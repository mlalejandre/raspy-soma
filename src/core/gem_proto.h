#ifndef SOMA_GEM_PROTO_H
#define SOMA_GEM_PROTO_H
#include <stdint.h>
/* PARCHE 069: MAC Ethernet Cadence GEM (la "eth" del RP1 de la Pi 5). Capa pura (registros, descriptores de 64 bits, MDIO), probada en el
 * host. Registros y descriptores segun el IP de Cadence tal como los usan el modelo de QEMU y macb de Linux; NO verificado en una placa. */
#define GEM_NWCTRL     0x000u
#define GEM_NWCFG      0x004u
#define GEM_NWSTATUS   0x008u
#define GEM_DMACFG     0x010u
#define GEM_TXSTATUS   0x014u
#define GEM_RXQBASE    0x018u
#define GEM_TXQBASE    0x01Cu
#define GEM_RXSTATUS   0x020u
#define GEM_ISR        0x024u
#define GEM_IDR        0x02Cu
#define GEM_PHYMNTNC   0x034u
#define GEM_SPADDR1LO  0x088u
#define GEM_SPADDR1HI  0x08Cu
#define GEM_TBQPH      0x4C8u
#define GEM_RBQPH      0x4D4u
#define GEM_NWCTRL_RXEN   (1u << 2)
#define GEM_NWCTRL_TXEN   (1u << 3)
#define GEM_NWCTRL_MDIO   (1u << 4)
#define GEM_NWCTRL_TSTART (1u << 9)
#define GEM_NWSTATUS_MDIO_IDLE (1u << 2)
#define GEM_DESC_WORDS 4u                                  /* 16 bytes: direccion de 64 bits */
#define GEM_FRAME_MAX  1536u
#define GEM_RXBUF_SIZE 2048u                               /* multiplo de 64 */
/* PARCHE 073: PHY estandar (IEEE 802.3 clausula 22) y resolucion de la negociacion automatica */
#define MII_BMCR   0u
#define MII_BMSR   1u
#define MII_ANAR   4u
#define MII_ANLPAR 5u
#define MII_GBCR   9u
#define MII_GBSR   10u
#define BMCR_RESET   0x8000u
#define BMCR_ANENABLE 0x1000u
#define BMCR_ANRESTART 0x0200u
#define BMSR_LINK    0x0004u
#define BMSR_ANDONE  0x0020u
#define ANAR_ALL     0x01E1u   /* 10/100 half+full, selector 802.3 */
#define GBCR_1000FULL 0x0200u
#define GBSR_LP1000FULL 0x0800u
struct gem_link { int up; uint32_t mbps; int full; };            /* mbps: 10, 100 o 1000 (0 si no hay enlace) */
struct gem_link gem_link_resolve(uint16_t bmsr, uint16_t anar, uint16_t lpa, uint16_t gbcr, uint16_t gbsr);
uint32_t gem_nwcfg_link(uint32_t mbps, int full);                /* como gem_nwcfg pero con velocidad y duplex negociados */
uint32_t gem_nwcfg(int gigabit);                           /* duplex completo, FCS fuera, trama de 1536, MDC /48, bus de 64 bits */
uint32_t gem_dmacfg(uint32_t rxbuf);                       /* descriptores de 64 bits + tamano de buffer RX + rafaga 16; 0 si rxbuf no es multiplo de 64 */
uint32_t gem_mdio_cmd(int read, uint32_t phy, uint32_t reg, uint16_t data);
void gem_mac_to_regs(const uint8_t mac[6], uint32_t *lo, uint32_t *hi);
void gem_mac_from_regs(uint32_t lo, uint32_t hi, uint8_t mac[6]);
int  gem_mac_valid(const uint8_t mac[6]);                  /* ni todo ceros, ni multicast */
void gem_rx_desc_init(uint32_t d[GEM_DESC_WORDS], uint64_t bus, int wrap);   /* del hardware */
void gem_rx_desc_release(uint32_t d[GEM_DESC_WORDS]);      /* lo devuelve al hardware (conserva WRAP y la direccion) */
int  gem_rx_sw_owned(const uint32_t d[GEM_DESC_WORDS]);    /* el hardware ya escribio una trama */
int  gem_rx_whole(const uint32_t d[GEM_DESC_WORDS]);       /* SOF y EOF: la trama cabe en un buffer */
uint32_t gem_rx_len(const uint32_t d[GEM_DESC_WORDS]);
void gem_tx_desc_init(uint32_t d[GEM_DESC_WORDS], int wrap);                      /* libre (USED) */
int  gem_tx_desc_fill(uint32_t d[GEM_DESC_WORDS], uint64_t bus, uint32_t len, int wrap);   /* 0 = ok; -1 = longitud invalida */
int  gem_tx_done(const uint32_t d[GEM_DESC_WORDS]);
uint32_t gem_next(uint32_t i, uint32_t n);
#endif
