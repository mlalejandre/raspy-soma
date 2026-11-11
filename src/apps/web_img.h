#ifndef SOMA_WEB_IMG_H
#define SOMA_WEB_IMG_H
#include <stdint.h>
/* Decodificadores de imagen del navegador: PNG, GIF (primer fotograma), JPEG (base y progresivo), BMP. Salida ARGB (alfa 0xFF = opaco). */
struct wimg { int w, h; uint32_t *px; };
#define WI_MAX_PIXELS (6u * 1024u * 1024u)
int  wi_decode(const uint8_t *d, uint32_t n, struct wimg *o);   /* 0 = OK */
void wi_free(struct wimg *o);
#endif
