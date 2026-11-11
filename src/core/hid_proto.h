#ifndef SOMA_HID_PROTO_H
#define SOMA_HID_PROTO_H
/* PARCHE 047: logica pura de USB HID (teclado y raton en protocolo "boot"): informe de teclado -> scancodes PS/2 (juego 1) y
 * decodificacion del informe de raton. Probada en el host (tests/test_hid.c). */
#include <stdint.h>
/* Escribe en out el scancode (1 byte, o 0xE0 + byte) de la tecla con ese "usage" HID (0x04..0x65). Devuelve cuantos bytes (0 = sin equivalente). */
int hid_usage_to_set1(uint8_t usage, uint8_t *out);
/* Compara dos informes de teclado de 8 bytes (modificadores, reservado, 6 teclas) y escribe los scancodes PS/2 de las pulsaciones y
 * liberaciones. Orden: modificadores pulsados, teclas liberadas, teclas pulsadas, modificadores liberados. Un informe de
 * desbordamiento (tecla 0x01) se ignora. Devuelve el numero de bytes escritos (maximo max; nunca parte una secuencia E0 xx). */
int hid_kbd_diff(const uint8_t prev[8], const uint8_t cur[8], uint8_t *out, int max);
struct hid_mouse { int8_t dx, dy, wheel; uint8_t buttons; };           /* dy positivo = hacia abajo (convencion HID) */
int hid_mouse_parse(const uint8_t *r, uint32_t len, struct hid_mouse *m);   /* 0 si el informe es demasiado corto */
#endif
