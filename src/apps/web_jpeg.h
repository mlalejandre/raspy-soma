#ifndef SOMA_WEB_JPEG_H
#define SOMA_WEB_JPEG_H
#include <stdint.h>
#include "web_img.h"
/* JPEG progresivo (y base) solo enteros; complementa a core/jpeg.c que solo hace base. 0 = OK. */
int wj_decode_progressive(const uint8_t *d, uint32_t n, struct wimg *o);
#endif
