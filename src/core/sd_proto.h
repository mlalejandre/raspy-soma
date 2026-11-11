#ifndef SOMA_SD_PROTO_H
#define SOMA_SD_PROTO_H
/* PARCHE 061: partes puras del protocolo SD / SDHCI (sin acceso a hardware): palabra de orden, divisor de reloj y capacidad (CSD). */
#include <stdint.h>

enum { SD_R_NONE = 0, SD_R1, SD_R1B, SD_R2, SD_R3, SD_R6, SD_R7 };
/* Registro COMMAND de SDHCI (16 bits): indice, tipo de respuesta, comprobaciones de CRC/indice y bit de datos. */
uint32_t sd_cmd_word(uint8_t idx, int resp, int data);
/* Divisor de reloj de SDHCI v3 (10 bits): f = base / (2*N), N = 0 -> base. Devuelve N. */
uint32_t sd_clk_div(uint32_t base_hz, uint32_t target_hz);
/* Valor del registro CLOCK_CONTROL con el divisor N y el reloj interno activado (sin SD_CLK_EN). */
uint32_t sd_clock_reg(uint32_t n);
/* Capacidad en sectores de 512 bytes a partir de la respuesta R2 de SDHCI (CRC ya quitado: bits 127:8 en resp[3..0]). 0 = ok. */
int sd_csd_sectors(const uint32_t resp[4], uint64_t *sectors);
#endif
