#ifndef SOMA_MEDIA_H
#define SOMA_MEDIA_H

#include <stdint.h>

/* SOMA_PARCHE_023_MEDIA: reproductor multimedia (ventana de la GUI). Reproduce video AVI (MJPG + PCM) y audio WAV del
 * almacen BIGFS. Un hilo del kernel (mediad) decodifica y alimenta el audio; la ventana solo dibuja el ultimo fotograma. */
void media_init(void);
void media_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  media_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void media_handle_key(char c);

int  media_play(const char *path);          /* 0 = peticion aceptada (la reproduccion empieza en el hilo); <0 error */
void media_pause_toggle(void);
void media_stop(void);
void media_seek_rel(int32_t seconds);
void media_seek_abs(uint32_t seconds);
int  media_command(const char *args, char *out, uint32_t max);   /* comando de shell 'media ...' */

#endif
