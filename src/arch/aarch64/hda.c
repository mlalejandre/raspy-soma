/* SOMA_PARCHE_020_AUDIO: marcador de posicion para AArch64 */
#include "hda.h"
int  hda_init(void) { return -1; }
const struct audio_hw *hda_hw(void) { return 0; }
int  hda_ready(void) { return 0; }
int  hda_set_volume(int percent) { (void)percent; return -1; }
int  hda_format_for_rate(uint32_t rate, uint16_t *fmt) { (void)rate; (void)fmt; return -1; }
void hda_info(char *out, uint32_t max) { const char *s = "audio: no disponible en AArch64"; uint32_t i = 0; while (s[i] && i + 1 < max) { out[i] = s[i]; i++; } if (max) out[i] = 0; }
