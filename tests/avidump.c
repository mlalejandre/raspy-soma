/* SOMA 022: herramienta de host: recorre un AVI con avi.c, decodifica cada fotograma con jpeg.c y escribe
 * SALIDA.rgb (RGB24 concatenado) y SALIDA.pcm (audio PCM16 tal cual). Para compararlo con ffmpeg (tests/test_video.py). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/avi.h"
#include "../src/core/jpeg.h"
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)l); if (fread(b, 1, (size_t)l, f) != (size_t)l) return 2; fclose(f);
    struct avi_info in; int r = avi_parse(b, (size_t)l, &in); if (r) { fprintf(stderr, "avi: %s\n", avi_strerror(r)); return 3; }
    char p[600]; snprintf(p, sizeof p, "%s.rgb", argv[2]); FILE *fr = fopen(p, "wb");
    snprintf(p, sizeof p, "%s.pcm", argv[2]); FILE *fa = fopen(p, "wb");
    uint32_t *px = calloc((size_t)in.width * in.height, 4);
    uint64_t pos = in.movi_start, d; struct avi_chunk c; int w; long nf = 0;
    while ((w = avi_walk(b, (size_t)l, &in, &pos, &c, &d)) == 1) {
        if (c.kind == AVI_K_VIDEO && c.stream == in.video_stream) {
            r = jpeg_decode(b + d, c.size, px, in.width, in.height, NULL);
            if (r) { fprintf(stderr, "frame %ld: %s\n", nf, jpeg_strerror(r)); return 3; }
            for (size_t i = 0; i < (size_t)in.width * in.height; ++i) { unsigned char q[3] = { (px[i] >> 16) & 255, (px[i] >> 8) & 255, px[i] & 255 }; fwrite(q, 1, 3, fr); }
            nf++;
        } else if (c.kind == AVI_K_AUDIO && c.stream == in.audio_stream) fwrite(b + d, 1, c.size, fa);
    }
    fclose(fr); fclose(fa);
    printf("%u %u %ld %u %u\n", in.width, in.height, nf, in.rate, in.channels);
    return w == 0 ? 0 : 3;
}
