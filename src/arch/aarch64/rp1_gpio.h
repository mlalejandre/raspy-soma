#ifndef SOMA_RP1_GPIO_H
#define SOMA_RP1_GPIO_H
#include <stdint.h>
/* PARCHE 071: GPIO de la Pi 5 (en el RP1). Ejecuta el comando "gpio" y escribe el resultado en out. 0 = ok, -1 = sin RP1 / error de uso. */
int rp1_gpio_run(const char *args, char *out, uint32_t max);
/* PARCHE 077: ventilador (GPIO45): 1 = en marcha, 0 = parado. 0 = ok, -1 = sin RP1. */
int rp1_fan_set(int on);
int rp1_fan_state(void);
int rp1_fan_inverted(void);
void rp1_fan_invert(void);
#endif
