#ifndef SOMA_GEM_H
#define SOMA_GEM_H
#include <stdint.h>
/* PARCHE 069: Ethernet de la Pi 5 (MAC Cadence GEM del RP1). Misma interfaz que virtio_net; la red de SOMA la usa a traves de virtio_net.c. */
int gem_init(void);                                 /* 1 = operativa; 0 = no hay RP1/GEM */
const uint8_t *gem_mac(void);
int gem_send(const void *frame, uint16_t len);      /* 0 = ok, -1 = error */
int gem_poll(void *out, uint16_t max, uint16_t *len_out);
void gem_stats(uint32_t *tx_pkts, uint32_t *rx_pkts, uint64_t *tx_b, uint64_t *rx_b);
int gem_link_up(void);
uint32_t gem_speed_mbps(void);   /* PARCHE 073: 10, 100, 1000 o 0 sin enlace */
int gem_full_duplex(void);
int gem_present(void);                              /* hay GEM operativa */
#endif
