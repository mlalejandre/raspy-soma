#ifndef SOMA_WEB_FONTDATA_H
#define SOMA_WEB_FONTDATA_H
/* DejaVu Sans (subconjunto Latin/Griego/Cirilico, sin hinting). Licencia Bitstream Vera / DejaVu: redistribucion libre.
 * Los bytes viven en src/apps/web_dejavu.ttf y se incrustan con .incbin (la ruta es relativa a la raiz del proyecto, desde donde corre make). */
#include <stdint.h>
__asm__(".section .rodata\n"
        ".balign 4\n"
        ".globl web_ttf_blob\n.globl web_ttf_blob_end\n"
        "web_ttf_blob:\n"
        ".incbin \"src/apps/web_dejavu.ttf\"\n"
        "web_ttf_blob_end:\n"
        ".byte 0\n"
        ".previous\n");
extern const uint8_t web_ttf_blob[];
extern const uint8_t web_ttf_blob_end[];
#define WEB_TTF_PTR web_ttf_blob
#define WEB_TTF_LEN ((uint32_t)(web_ttf_blob_end - web_ttf_blob))
#endif
