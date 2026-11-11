/* wavplay <ruta>: reproductor de WAV (PCM 16 bits, mono o estereo) que lee del almacen BIGFS en streaming y toca por
 * la salida de audio. Todo en espacio de usuario: open/read/seek + audio_open/write/close. */
#include "ulib.h"
#include "../core/wav.h"
#include "../core/wav.c"

static unsigned char rd[8192];
static short st[8192];                  /* mono -> estereo: cada muestra se duplica (16384 bytes) */

static char line[200];
static int ln;
static void cat(const char *s) { while (*s && ln < (int)sizeof line - 1) line[ln++] = *s++; }
static void catdec(u64 v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } cat(b + i); }

void _start(void)
{
    char path[96];
    if (u_getargs(path, sizeof path) <= 0) { u_puts("[wavplay] uso: exec -t 600000 /bin/wavplay.elf <ruta del almacen>\n"); u_exit(2); }
    i64 fd = u_open(path);
    if (fd < 0) { u_puts("[wavplay] no se puede abrir el archivo\n"); u_exit(3); }
    i64 n = u_read(fd, rd, 4096);
    struct wav_info w;
    int r = n > 0 ? wav_parse(rd, (size_t)n, &w) : WAV_E_SHORT;
    if (r != WAV_OK) { u_puts("[wavplay] "); u_puts(wav_strerror(r)); u_puts("\n"); u_exit(4); }
    i64 fsize = u_fsize(fd);
    u64 size = w.data_size;
    if (size == 0 || size > (u64)fsize || w.data_offset + size > (u64)fsize) size = (u64)fsize - w.data_offset;
    u_seek(fd, (i64)w.data_offset, 0);
    i64 ar = u_audio_open(w.rate);
    if (ar < 0) { u_puts("[wavplay] no se pudo abrir el audio: -"); u_putdec((u64)(-ar)); u_puts("\n"); u_exit(5); }
    u64 done = 0, played = 0; int ok = 1;
    while (done < size && ok) {
        u64 want = size - done < sizeof rd ? size - done : sizeof rd;
        i64 g = u_read(fd, rd, want);
        if (g <= 0) break;
        done += (u64)g;
        if (w.channels == 2) {
            u64 b = (u64)g & ~3ULL;
            if (b) { ok = u_audio_write(rd, b) == (i64)b; played += b; }
        } else {
            u64 samples = (u64)g / 2;
            for (u64 i = 0; i < samples; ++i) { short v = ((short *)rd)[i]; st[2 * i] = v; st[2 * i + 1] = v; }
            if (samples) { ok = u_audio_write(st, samples * 4) == (i64)(samples * 4); played += samples * 4; }
        }
    }
    u_audio_close(1);
    u_close(fd);
    cat("[wavplay] "); cat(path); cat(": "); catdec(played); cat(" bytes tocados, "); catdec(w.rate); cat(" Hz, ");
    catdec(w.channels); cat(w.channels == 1 ? " canal" : " canales"); cat(ok ? "\n" : " (INTERRUMPIDO)\n");
    u_write(line, (u64)ln);
    u_exit(ok ? 0 : 1);
}
