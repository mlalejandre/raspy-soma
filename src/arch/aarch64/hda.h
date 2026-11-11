#ifndef MYOS_HDA_H
#define MYOS_HDA_H
#include <stdint.h>
#include "audio.h"
/* SOMA_PARCHE_020_AUDIO: sin audio en AArch64 todavia */
int  hda_init(void);
const struct audio_hw *hda_hw(void);
int  hda_ready(void);
int  hda_set_volume(int percent);
int  hda_format_for_rate(uint32_t rate, uint16_t *fmt);
void hda_info(char *out, uint32_t max);
#endif
