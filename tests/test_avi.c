/* SOMA 022: pruebas del lector AVI con archivos de tools/mkavi.py (+ .expect) y fuzz. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/core/avi.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FALLO: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(l ? (size_t)l : 1); if (fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)l; return b;
}

static long expect(const char *txt, const char *key)
{
    char k[32]; long v; const char *p = txt;
    while (sscanf(p, "%31s %ld", k, &v) == 2) { if (!strcmp(k, key)) return v; p = strchr(p, '\n'); if (!p) break; p++; }
    return -1;
}

static void one(const char *path)
{
    size_t l, el; char ep[600]; snprintf(ep, sizeof ep, "%s.expect", path);
    uint8_t *b = slurp(path, &l); char *e = (char *)slurp(ep, &el);
    if (!b || !e) { printf("FALLO: no leo %s\n", path); fails++; return; }
    e = realloc(e, el + 1); e[el] = 0;
    struct avi_info in; int r = avi_parse(b, l, &in);
    CHECK(r == 0, "%s parse=%d", path, r);
    if (r == 0) {
        CHECK((long)in.width == expect(e, "width") && (long)in.height == expect(e, "height"), "%s tamano", path);
        CHECK((long)in.usec_per_frame == expect(e, "usec"), "%s usec %u", path, in.usec_per_frame);
        CHECK(in.has_audio == expect(e, "audio"), "%s audio", path);
        if (in.has_audio) { CHECK((long)in.rate == expect(e, "rate") && (long)in.channels == expect(e, "channels"), "%s formato audio", path); }
        uint64_t pos = in.movi_start, d; struct avi_chunk c; long nv = 0, ab = 0, vb = 0; int w;
        while ((w = avi_walk(b, l, &in, &pos, &c, &d)) == 1) {
            if (c.kind == AVI_K_VIDEO && c.stream == in.video_stream) { nv++; vb += c.size; CHECK(b[d] == 0xFF && b[d + 1] == 0xD8, "%s frame no es JPEG", path); }
            else if (c.kind == AVI_K_AUDIO && c.stream == in.audio_stream) ab += c.size;
        }
        CHECK(w == 0, "%s recorrido=%d", path, w);
        CHECK(nv == expect(e, "frames"), "%s frames %ld", path, nv);
        CHECK(ab == expect(e, "abytes") || !in.has_audio, "%s audio bytes %ld", path, ab);
        CHECK(vb == expect(e, "vbytes"), "%s video bytes %ld", path, vb);
        /* igual recorrido leyendo solo cabeceras de 8 bytes (como hace el reproductor) */
        uint64_t off = in.movi_start; long nv2 = 0;
        while (off + 8 <= in.movi_end) { struct avi_chunk c2; avi_chunk_parse(b + off, &c2); if (c2.kind == AVI_K_VIDEO) nv2++; off = avi_chunk_next(off, &c2); }
        CHECK(nv2 == nv, "%s recorrido por cabeceras", path);
    }
    /* cabeceras con menos bytes: nunca falla de forma insegura; los prefijos largos deben dar OK o TRUNC */
    for (size_t cut = 0; cut < l && cut < 2000; cut += 7) { struct avi_info t; uint8_t *c = malloc(cut ? cut : 1); memcpy(c, b, cut); int rr = avi_parse(c, cut, &t); (void)rr; free(c); }
    /* fuzz */
    uint64_t seed = 99; uint8_t *m = malloc(l);
    size_t hl = l < 4096 ? l : 4096;
    for (int it = 0; it < 3000; ++it) {
        memcpy(m, b, hl);
        int k = 1 + (int)((seed >> 33) % 5); seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        while (k--) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; m[(seed >> 24) % hl] = (uint8_t)(seed >> 50); }
        struct avi_info t; uint8_t *c = malloc(hl); memcpy(c, m, hl);
        if (avi_parse(c, hl, &t) == 0) {
            uint64_t pos = t.movi_start, d; struct avi_chunk ch; int w, n = 0;
            while ((w = avi_walk(c, hl, &t, &pos, &ch, &d)) == 1 && n++ < 100000) { if (d + ch.size > hl && ch.kind != AVI_K_LIST) { CHECK(0, "walk se sale"); break; } }
        }
        free(c);
    }
    free(m); free(b); free(e);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) if (!strstr(argv[i], ".expect") && strstr(argv[i], ".avi") && strlen(argv[i]) > 4 && !strcmp(argv[i] + strlen(argv[i]) - 4, ".avi")) one(argv[i]);
    struct avi_info in;
    CHECK(avi_parse((const uint8_t *)"RIFF\0\0\0\0WAVE", 12, &in) == AVI_E_FORMAT, "WAVE no es AVI");
    CHECK(avi_parse((const uint8_t *)"", 0, &in) == AVI_E_FORMAT, "vacio");
    printf("avi: %s\n", fails ? "FALLOS" : "OK");
    return fails ? 1 : 0;
}
