#ifndef SOMA_GPIO_PROTO_H
#define SOMA_GPIO_PROTO_H
#include <stdint.h>
/* PARCHE 071: GPIO del RP1 (40 pines de la Pi 5: GPIO0..27 en el RP1). Capa pura, probada en el host. Tres bloques: IO_BANK0 (STATUS y CTRL por pin, funcion),
 * SYS_RIO0 (salida OUT, habilitar salida OE, entrada IN; alias de XOR/SET/CLR a +0x1000/+0x2000/+0x3000) y PADS_BANK0 (electrico: IE, OD, pull-up/down).
 * Valores segun el datasheet publico del RP1 y el controlador pinctrl de Linux; NO verificados en una placa. */
#define GPIO_COUNT        28u
#define GPIO_FUNC_RIO     5u            /* funcion "gpio" por software (SYS_RIO) */
#define GPIO_RIO_OUT      0x0u
#define GPIO_RIO_OE       0x4u
#define GPIO_RIO_IN       0x8u
#define GPIO_ALIAS_XOR    0x1000u
#define GPIO_ALIAS_SET    0x2000u
#define GPIO_ALIAS_CLR    0x3000u
enum gpio_pull { GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN };
enum gpio_op { GPIO_OP_LIST, GPIO_OP_READ, GPIO_OP_IN, GPIO_OP_OUT0, GPIO_OP_OUT1, GPIO_OP_TOGGLE, GPIO_OP_PULL_UP, GPIO_OP_PULL_DOWN, GPIO_OP_PULL_NONE };
struct gpio_cmd { int op; uint32_t pin; };
int gpio_valid(uint32_t pin);
uint32_t gpio_status_off(uint32_t pin);        /* dentro de IO_BANK0 */
uint32_t gpio_ctrl_off(uint32_t pin);
uint32_t gpio_pad_off(uint32_t pin);           /* dentro de PADS_BANK0 (el registro 0 es la tension) */
uint32_t gpio_ctrl_with_func(uint32_t ctrl, uint32_t func);
uint32_t gpio_func_of(uint32_t ctrl);
uint32_t gpio_pad_value(uint32_t old, int input_enable, int output_disable, enum gpio_pull pull);
int gpio_pad_ie(uint32_t pad);
int gpio_pad_od(uint32_t pad);
enum gpio_pull gpio_pad_pull(uint32_t pad);
uint32_t gpio_pin_mask(uint32_t pin);          /* 1 << pin */
/* Parser del comando: "", "N", "N in|out|0|1|toggle|up|down|nopull". 0 = ok, -1 = sintaxis o pin invalido. */
int gpio_parse(const char *args, struct gpio_cmd *out);
#endif
