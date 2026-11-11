/* SOMA 022: herramienta de host: decodifica un JPEG con el MISMO decodificador que usa vidplay y escribe un PPM (P6). */
#include <stdio.h>
#include <stdlib.h>
#include "../src/core/jpeg.h"
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: jpeg2ppm in.jpg out.ppm\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)l); if (fread(b, 1, (size_t)l, f) != (size_t)l) return 2; fclose(f);
    struct jpeg_info in;
    if (jpeg_get_info(b, (size_t)l, &in)) return 3;
    uint32_t *px = calloc((size_t)in.width * in.height, 4);
    int r = jpeg_decode(b, (size_t)l, px, in.width, in.height, &in);
    if (r) { fprintf(stderr, "%s\n", jpeg_strerror(r)); return 3; }
    FILE *o = fopen(argv[2], "wb"); fprintf(o, "P6\n%u %u\n255\n", in.width, in.height);
    for (size_t i = 0; i < (size_t)in.width * in.height; ++i) { unsigned char c[3] = { (px[i] >> 16) & 255, (px[i] >> 8) & 255, px[i] & 255 }; fwrite(c, 1, 3, o); }
    fclose(o); return 0;
}
