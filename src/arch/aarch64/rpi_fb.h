#ifndef SOMA_RPI_FB_H
#define SOMA_RPI_FB_H

#include <stdint.h>
#include <stddef.h>

int  rpi_fb_init(void);
void rpi_fb_putc(char c);
void rpi_fb_clear(void);
int  rpi_fb_console_active(void);   /* PARCHE 075: 1 = consola de texto en HDMI activa */
int  rpi_fb_last_error(void);

/* PARCHE 063: framebuffer del VideoCore por el mailbox (canal de propiedades). Pide w x h a 32 bits; si el firmware no da memoria
 * suficiente prueba tamanos menores (hasta 800x600). 0 = bien (*vram, *ow, *oh rellenados), <0 = sin mailbox o sin respuesta. */
int  rpi_fb_set_mode(uint32_t w, uint32_t h, volatile uint32_t **vram, uint32_t *ow, uint32_t *oh, uint32_t *obpp, uint32_t *opitch);
void rpi_fb_info(void);
/* PARCHE 079: mensaje de propiedades del firmware (buffer alineado a 16 bytes, bajo 4 GiB). 0 = bien. */
int  rpi_mbox_prop(uint32_t *buf, uint32_t bytes);
int  rpi_fb_swap_rb(void);          /* PARCHE 076 */
void rpi_fb_toggle_swap(void);
/* Mantenimiento de cache: limpia (escribe a RAM) o invalida un rango. Necesario porque la pantalla y el firmware leen RAM sin pasar por la
 * cache de la CPU. */
void soma_dcache_clean(const volatile void *p, size_t n);
void soma_dcache_flush_inval(const volatile void *p, size_t n);

#endif
