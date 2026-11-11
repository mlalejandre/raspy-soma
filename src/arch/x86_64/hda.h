#ifndef MYOS_HDA_H
#define MYOS_HDA_H

#include <stdint.h>
#include "audio.h"

/* SOMA_PARCHE_020_AUDIO: controlador Intel HD Audio (ICH6/ICH9; en QEMU: -device intel-hda -device hda-duplex).
 * Sin interrupciones: la posicion del DMA se consulta (LPIB). Una salida PCM de 16 bits estereo. */
int  hda_init(void);                               /* 0 = listo; <0 = no hay controlador/codec utilizable. Imprime el estado */
const struct audio_hw *hda_hw(void);               /* ganchos para el nucleo de audio (valido si hda_init() devolvio 0) */
int  hda_ready(void);
int  hda_set_volume(int percent);                  /* 0..100 sobre los amplificadores del camino de salida */
int  hda_format_for_rate(uint32_t rate, uint16_t *fmt);   /* formato de flujo HDA de una frecuencia; 0 si es soportada */
void hda_info(char *out, uint32_t max);

#endif
