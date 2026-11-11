#ifndef SOMA_SDHCI_H
#define SOMA_SDHCI_H
/* PARCHE 061: tarjeta SD por SDHCI (Pi 5: SDIO1). Sondeo (PIO), un bloque de 512 bytes por transferencia de datos. */
#include <stdint.h>
struct sd_info { uint64_t sectors; int sdhc; uint16_t rca; uint8_t sector0[16]; char name[8]; int err; };
int  sd_probe(struct sd_info *o);                       /* 0 = tarjeta lista (idempotente) */
int  sd_ready(void);
const struct sd_info *sd_get(void);
int  sd_read(uint64_t lba, uint32_t n, void *buf);      /* n <= 128; 0 = OK */
int  sd_write(uint64_t lba, uint32_t n, const void *buf);
const char *sd_err_str(int e);
void sd_autostart(void);
#endif
