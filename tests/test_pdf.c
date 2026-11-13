/* PARCHE 088: pruebas del lector de PDF en el anfitrion.
 *   test_pdf archivo.pdf [salida]   vuelca las operaciones de cada pagina (las usa tests/pdf_check.py)
 *   test_pdf --fuzz N semilla archivo.pdf...   mutaciones al azar con ASAN/UBSAN */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pdf.h"
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
void kprint(const char *s) { (void)s; }
void jpeg_yield_hook(void) {}
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
static uint8_t *slurp(const char *f, uint32_t *n)
{
    FILE *fp = fopen(f, "rb"); if (!fp) return 0;
    fseek(fp, 0, SEEK_END); long l = ftell(fp); rewind(fp);
    uint8_t *d = malloc((size_t)l + 1); *n = (uint32_t)fread(d, 1, (size_t)l, fp); fclose(fp); return d;
}
static void dump(const char *file, const char *out)
{
    uint32_t n; uint8_t *d = slurp(file, &n); if (!d) { printf("no se puede leer %s\n", file); exit(1); }
    char err[96]; struct pdf *P = pdf_open(d, n, err, sizeof err);
    if (!P) { printf("ERROR %s: %s\n", file, err); free(d); exit(2); }
    FILE *o = out ? fopen(out, "w") : stdout;
    int np = pdf_page_count(P); char title[128]; pdf_title(P, title, sizeof title);
    fprintf(o, "DOC %d %s\n", np, title);
    for (int i = 0; i < np && i < 6; i++) {
        struct pdf_page pg; pdf_render_page(P, i, 384, &pg);
        fprintf(o, "PAGE %d %d %d %d\n", i, pg.w, pg.h, pg.unknown);
        for (int k = 0; k < pg.nops; k++) {
            struct pdf_op *op = &pg.ops[k];
            if (op->kind == PDF_OP_TEXT) fprintf(o, "T %d %d %d %d %d %06x %.*s\n", op->x, op->y, op->w, op->size, op->flags, op->color, op->len, op->text ? op->text : "");
            else if (op->kind == PDF_OP_PATH) { fprintf(o, "P %d %06x %06x %d %d", op->flags, op->fill, op->color, op->size, op->npts); for (int j = 0; j < op->npts; j++) fprintf(o, " %d %d", op->pts[2 * j], op->pts[2 * j + 1]); fprintf(o, "\n"); }
            else if (op->kind == PDF_OP_IMAGE) {
                static int ni; char fn[64]; snprintf(fn, sizeof fn, "/tmp/pdft/img%d.ppm", ni++);
                FILE *f = fopen(fn, "wb"); fprintf(f, "P6 %d %d 255\n", op->iw, op->ih); for (int j = 0; j < op->iw * op->ih; j++) { uint32_t c = op->img[j]; uint8_t a = c >> 24; uint8_t px[3] = { (uint8_t)(255 - ((255 - ((c >> 16) & 255)) * a / 255)), (uint8_t)(255 - ((255 - ((c >> 8) & 255)) * a / 255)), (uint8_t)(255 - ((255 - (c & 255)) * a / 255)) }; fwrite(px, 1, 3, f); } fclose(f);
                fprintf(o, "I %d %d %d %d %d %d %s\n", op->x, op->y, op->w, op->h, op->flip_x, op->flip_y, fn);
            }
        }
        if (i == 0) { static char txt[200000]; pdf_page_text(&pg, txt, sizeof txt); FILE *t = fopen("/tmp/pdft/text0.txt", "w"); if (t) { fputs(txt, t); fclose(t); } }
        pdf_page_free(&pg);
    }
    if (out) fclose(o);
    pdf_close(P); free(d);
}
static uint64_t S = 88172645463325252ULL;
static uint32_t rnd(void) { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32_t)(S >> 11); }
int main(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "--fuzz")) {
        long iters = atol(argv[2]); S ^= (uint64_t)atol(argv[3]) * 0x9E3779B97F4A7C15ULL;
        for (int a = 4; a < argc; a++) {
            uint32_t n; uint8_t *seed = slurp(argv[a], &n); if (!seed) continue;
            for (long it = 0; it < iters; it++) {
                uint32_t m = n; uint8_t *b = malloc(n + 1); memcpy(b, seed, n);
                int k = 1 + rnd() % 8;
                while (k--) switch (rnd() % 5) {
                    case 0: b[rnd() % m] ^= (uint8_t)(1u << (rnd() % 8)); break;
                    case 1: b[rnd() % m] = (uint8_t)rnd(); break;
                    case 2: if (m > 64) m = 32 + rnd() % (m - 32); break;
                    case 3: { uint32_t p = rnd() % m; b[p] = "0123456789<>[]()/ R"[rnd() % 19]; } break;
                    case 4: { uint32_t p = rnd() % m, l = 1 + rnd() % 64; if (p + l < m) { memmove(b + p, b + p + l, m - p - l); m -= l; } } break;
                }
                uint8_t *c = malloc(m ? m : 1); memcpy(c, b, m); free(b);
                char err[96]; struct pdf *P = pdf_open(c, m, err, sizeof err);
                if (P) { int np = pdf_page_count(P); for (int i = 0; i < np && i < 3; i++) { struct pdf_page pg; if (pdf_render_page(P, i, 200, &pg) == 0) { char t[512]; pdf_page_text(&pg, t, sizeof t); } pdf_page_free(&pg); } pdf_close(P); }
                free(c);
            }
            printf("ok %s\n", argv[a]); free(seed);
        }
        printf("FUZZ-PDF OK\n"); return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--check")) {   /* PDF de tests/pdf_seeds.py: deben salir el texto, los acentos y el titulo */
        int fails = 0;
        for (int a = 2; a < argc; a++) {
            uint32_t n; uint8_t *d = slurp(argv[a], &n); char err[96];
            struct pdf *P = d ? pdf_open(d, n, err, sizeof err) : 0;
            if (!P) { printf("  FALLO %s: %s\n", argv[a], d ? err : "no se lee"); fails++; free(d); continue; }
            struct pdf_page pg; char t[2048] = "";
            if (pdf_page_count(P) != 1 || pdf_render_page(P, 0, 256, &pg) != 0) { printf("  FALLO %s: paginas\n", argv[a]); fails++; }
            else {
                pdf_page_text(&pg, t, sizeof t);
                if (!strstr(t, "Hola SOMA")) { printf("  FALLO %s: texto '%s'\n", argv[a], t); fails++; }
                if (!strstr(t, "\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba")) { printf("  FALLO %s: acentos '%s'\n", argv[a], t); fails++; }
                int paths = 0; for (int i = 0; i < pg.nops; i++) if (pg.ops[i].kind == PDF_OP_PATH) paths++;
                if (paths < 2) { printf("  FALLO %s: %d trayectos\n", argv[a], paths); fails++; }
                pdf_page_free(&pg);
            }
            pdf_close(P); free(d);
        }
        printf("test_pdf: %s\n", fails ? "FALLOS" : "OK"); return fails ? 1 : 0;
    }
    if (argc < 2) { printf("uso: test_pdf archivo.pdf [salida]\n"); return 1; }
    dump(argv[1], argc > 2 ? argv[2] : 0);
    return 0;
}
