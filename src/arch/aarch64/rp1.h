#ifndef SOMA_RP1_H
#define SOMA_RP1_H
#include <stdint.h>
/* PARCHE 067: RP1, el chip de E/S de la Pi 5 (USB, Ethernet, GPIO) que cuelga de PCIe2. Sin verificar en una placa real. */
int rp1_init(void);                                 /* 0 = RP1 encontrado y BAR1 asignada; <0 = no hay (QEMU no lo emula) */
int rp1_present(void);
uint64_t rp1_bar1_cpu(void);
uint32_t rp1_chip_id(void);                         /* primer registro de SYSINFO */
uint64_t rp1_periph_base(int id);                  /* PARCHE 069: direccion de CPU de un periferico (enum rp1_periph); 0 si no hay */
uint64_t rp1_xhci_base(int n);                      /* direccion de CPU del xHCI n (0 o 1); 0 si no hay */
void rp1_format(char *out, uint32_t max);
#endif
