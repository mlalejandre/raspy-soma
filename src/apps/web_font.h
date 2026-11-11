#ifndef SOMA_WEB_FONT_H
#define SOMA_WEB_FONT_H
#include <stdint.h>
/* Texto proporcional antialiasado (DejaVu Sans incrustada) para el motor web. Anchos en 1/64 px. */
#define WF_BOLD   1
#define WF_ITALIC 2
#define WF_MONO   4
int  wf_init(void);                                             /* 1 si hay fuente */
int  wf_width64(const char *s, uint32_t n, int px, int fl);     /* ancho en 1/64 px (UTF-8) */
int  wf_width(const char *s, uint32_t n, int px, int fl);       /* ancho en px (redondeado arriba) */
int  wf_cp_adv64(uint32_t cp, int px, int fl);
void wf_draw(int32_t x, int32_t baseline_y, const char *s, uint32_t n, int px, uint32_t rgb, int fl, int alpha);
int  wf_ascent(int px);
int  wf_descent(int px);
#endif
