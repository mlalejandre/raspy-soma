#ifndef SOMA_RP1_PROTO_H
#define SOMA_RP1_PROTO_H
#include <stdint.h>
/* PARCHE 067: mapa de perifericos del RP1 (Raspberry Pi 5). Capa pura, probada en el host.
 * Fuente: datasheet publico "RP1 Peripherals" v1.1, tabla de direcciones 2.3. El RP1 es un endpoint PCIe (vendor 0x1de4) cuya BAR1 da
 * acceso a los perifericos: offset en la BAR1 = direccion del datasheet - 0x40000000. NO verificado en una placa real. */
#define RP1_VID          0x1DE4u
#define RP1_DID_C0       0x0001u
#define RP1_BAR1_MIN     0x400000ULL            /* 4 MiB: lo que cubre el mapa usado aqui (valor esperado; el tamano real lo da el BAR) */
enum rp1_periph {
    RP1_SYSINFO, RP1_CLOCKS_MAIN, RP1_CLOCKS_VIDEO, RP1_ETH, RP1_ETH_CFG, RP1_PCIE_CFG, RP1_USB0_CFG, RP1_USB1_CFG,
    RP1_DMA, RP1_IO_BANK0, RP1_SYS_RIO0, RP1_PADS_BANK0, RP1_USB0, RP1_USB1, RP1_PERIPH_COUNT
};
int rp1_is_rp1(uint16_t vid, uint16_t did);                              /* 1 si es un RP1 conocido */
int64_t rp1_periph_off(int id);                                          /* offset dentro de la BAR1; -1 si no existe */
uint64_t rp1_periph_len(int id);                                         /* tamano reservado del bloque (0 si no existe) */
const char *rp1_periph_name(int id);
/* Direccion de CPU de un periferico dada la direccion de CPU de la BAR1 y su tamano; 0 si no cabe. */
uint64_t rp1_periph_cpu(uint64_t bar1_cpu, uint64_t bar1_size, int id);
#endif
