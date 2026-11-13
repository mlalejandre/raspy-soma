#ifndef SOMA_WEB_JPEG_H
#define SOMA_WEB_JPEG_H
#include <stdint.h>
#include "web_img.h"
/* JPEG progresivo (y base) solo enteros; complementa a core/jpeg.c que solo hace base. 0 = OK. */
int wj_decode_progressive(const uint8_t *d, uint32_t n, struct wimg *o);
/* PARCHE 086: igual, reduciendo la salida 1/scale (1, 2, 4, 8) en el dominio DCT; o->nw/nh = tamano natural */
int wj_decode(const uint8_t *d, uint32_t n, struct wimg *o, int scale);
#endif
