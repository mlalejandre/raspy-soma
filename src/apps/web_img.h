#ifndef SOMA_WEB_IMG_H
#define SOMA_WEB_IMG_H
#include <stdint.h>
/* Decodificadores de imagen del navegador: PNG, GIF (primer fotograma), JPEG (base y progresivo), BMP. Salida ARGB (alfa 0xFF = opaco). */
struct wimg { int w, h; uint32_t *px; int nw, nh; };   /* PARCHE 086: nw/nh = tamano natural si se decodifico reducida (0 = w/h) */
extern int wi_oom;   /* PARCHE 086: 1 si la ultima decodificacion se quedo sin memoria */
#define WI_MAX_PIXELS (6u * 1024u * 1024u)
int  wi_decode(const uint8_t *d, uint32_t n, struct wimg *o);   /* 0 = OK */
void wi_free(struct wimg *o);
/* PARCHE 086: decodifica sin bajar de maxw x maxh (los JPEG se reducen al decodificar si sobra tamano). -2 = sin memoria */
int  wi_decode_fit(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh);
#endif
