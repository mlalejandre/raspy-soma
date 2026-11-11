/* vidplay <ruta> [mantener_ms] [nodrop]: reproductor de video AVI (MJPG + PCM16) desde el almacen BIGFS.
 * Todo en espacio de usuario: open/read/seek, decodificador JPEG entero, audio_open/write/status y framebuffer.
 *
 * Sincronizacion: el reloj maestro es el reloj de AUDIO (bytes de datos que ya han sonado, SYS_AUDIO_STATUS); si no hay
 * audio (o ya se acabo de alimentar) se usa el tiempo del sistema anclado en el ultimo valor del reloj de audio.
 *   - un fotograma se muestra cuando reloj >= pts - 10 ms (pts = indice * periodo);
 *   - si ya va mas de 60 ms tarde se SALTA sin decodificarlo (cada JPEG es independiente); el ultimo siempre se muestra;
 *   - "nodrop" desactiva el salto (pruebas deterministas).
 * El mismo archivo se abre dos veces (uno para video y otro para audio), cada uno salta por sus chunks con seek. */
#include "ulib.h"
#include "../core/jpeg.h"
#include "../core/jpeg.c"
#include "../core/avi.h"
#include "../core/avi.c"

#define MAXPIX (1280 * 720)
#define VBUF_BYTES (192 * 1024)
#define LATE_US 60000
#define EARLY_US 10000

static unsigned int frame[MAXPIX];
static unsigned char vbuf[VBUF_BYTES];
static unsigned char hdr[65536];
static unsigned char araw[16384];
static short aout[16384];                       /* 32 KiB: hasta 16384 muestras estereo... (araw mono 8192 muestras -> 16384 shorts) */

static char line[300];
static int ln;
static void cat(const char *s) { while (*s && ln < (int)sizeof line - 1) line[ln++] = *s++; }
static void catdec(u64 v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } cat(b + i); }
static void flush(void) { u_write(line, (u64)ln); ln = 0; }

static struct avi_info av;
static i64 vfd = -1, afd = -1;
static int audio_on;
static u64 voff, aoff;                          /* siguiente chunk de cada lector */
static u64 achunk_off, achunk_rem;              /* chunk de audio en curso: donde sigue y cuanto queda */
static unsigned char carry[4]; static int ncarry;
static int audio_done;                          /* todo el audio esta en la FIFO del kernel */
static u64 fed_bytes;
static u64 anchor_us, anchor_wall;              /* reloj cuando el audio dejo de alimentarse / sin audio */
static int anchored;
static u64 last_audio_us;

static int read_hdr(i64 fd, u64 off, unsigned char h[8])
{
    if (u_seek(fd, (i64)off, 0) != (i64)off) return 0;
    return u_read(fd, h, 8) == 8;
}

/* siguiente chunk de audio (rellena achunk_*) */
static int next_audio_chunk(void)
{
    while (aoff + 8 <= av.movi_end) {
        unsigned char h[8]; struct avi_chunk c;
        if (!read_hdr(afd, aoff, h)) return 0;
        avi_chunk_parse(h, &c);
        u64 here = aoff;
        aoff = avi_chunk_next(aoff, &c);
        if (c.kind == AVI_K_AUDIO && c.stream == av.audio_stream && c.size > 0 && here + 8 + c.size <= av.movi_end) {
            achunk_off = here + 8; achunk_rem = c.size; return 1;
        }
    }
    return 0;
}

/* Mete en la FIFO del kernel todo lo que quepa (sin bloquear), hasta `limit` bytes por llamada. */
static void feed_audio(u64 limit)
{
    if (!audio_on || audio_done) return;
    u64 st[3];
    if (u_audio_status(st) != 0) return;
    u64 space = st[0], moved = 0;
    while (space >= 8192 && moved < limit) {
        if (achunk_rem == 0 && !next_audio_chunk()) { audio_done = 1; break; }
        u64 want = achunk_rem < 8192 ? achunk_rem : 8192;
        if (av.channels == 1 && want > 4096) want = 4096;
        for (int i = 0; i < ncarry; ++i) araw[i] = carry[i];
        if (u_seek(afd, (i64)achunk_off, 0) != (i64)achunk_off) { audio_done = 1; break; }
        i64 g = u_read(afd, araw + ncarry, want);
        if (g <= 0) { audio_done = 1; break; }
        achunk_off += (u64)g; achunk_rem -= (u64)g;
        u64 have = (u64)g + (u64)ncarry;
        u64 fb = av.channels == 2 ? 4 : 2;              /* bytes de entrada por muestra-trama */
        u64 use = have - have % fb;
        ncarry = (int)(have - use);
        for (int i = 0; i < ncarry; ++i) carry[i] = araw[use + (u64)i];
        if (use == 0) continue;
        i64 w;
        if (av.channels == 2) w = u_audio_write(araw, use);
        else {
            u64 n = use / 2;
            for (u64 i = 0; i < n; ++i) { aout[2 * i] = ((short *)araw)[i]; aout[2 * i + 1] = ((short *)araw)[i]; }
            w = u_audio_write(aout, n * 4);
        }
        u64 out_bytes = av.channels == 2 ? use : (use / 2) * 4;
        if (w != (i64)out_bytes) { audio_done = 1; break; }
        fed_bytes += out_bytes; space = space > out_bytes ? space - out_bytes : 0; moved += out_bytes;
    }
}

/* reloj maestro en us */
static u64 clock_us(void)
{
    u64 now = u_uptime_ms();
    if (audio_on && !audio_done) {
        u64 st[3];
        if (u_audio_status(st) == 0) { last_audio_us = st[1]; return st[1]; }
        return last_audio_us;
    }
    if (!anchored) { anchored = 1; anchor_wall = now; anchor_us = audio_on ? last_audio_us : 0; }
    return anchor_us + (now - anchor_wall) * 1000;
}

void _start(void)
{
    char arg[160];
    if (u_getargs(arg, sizeof arg) <= 0) { u_puts("[vidplay] uso: exec -b -t 600000 /bin/vidplay.elf <ruta del almacen> [mantener_ms] [nodrop]\n"); u_exit(2); }
    char path[96]; int pi = 0; const char *p = arg;
    while (*p && *p != ' ' && pi < (int)sizeof path - 1) path[pi++] = *p++;
    path[pi] = 0;
    u64 hold = 1500; int nodrop = 0;
    while (*p == ' ') p++;
    if (*p >= '0' && *p <= '9') { hold = 0; while (*p >= '0' && *p <= '9') hold = hold * 10 + (u64)(*p++ - '0'); }
    while (*p == ' ') p++;
    if (p[0] == 'n' && p[1] == 'o' && p[2] == 'd') nodrop = 1;

    vfd = u_open(path);
    if (vfd < 0) { u_puts("[vidplay] no se puede abrir el archivo\n"); u_exit(3); }
    i64 n = u_read(vfd, hdr, sizeof hdr);
    int r = n > 0 ? avi_parse(hdr, (size_t)n, &av) : AVI_E_TRUNC;
    if (r != AVI_OK) { u_puts("[vidplay] "); u_puts(avi_strerror(r)); u_puts("\n"); u_exit(4); }
    { i64 fsz = u_fsize(vfd); if (fsz > 0 && av.movi_end > (u64)fsz) av.movi_end = (uint32_t)fsz; }   /* archivo cortado: se toca lo que haya */
    if ((u64)av.width * av.height > MAXPIX) { u_puts("[vidplay] video demasiado grande (maximo 1280x720)\n"); u_exit(4); }

    /* primer JPEG: comprobar que se puede decodificar y que coincide con la cabecera */
    if (av.has_audio) {
        afd = u_open(path);
        if (afd < 0) av.has_audio = 0;
    }
    u64 sw = (av.width <= 800 && av.height <= 600) ? 800 : 1280, sh = (av.width <= 800 && av.height <= 600) ? 600 : 720;
    i64 fr = u_fb_open(sw, sh);
    if (fr < 0) { cat("[vidplay] no se pudo abrir la pantalla: -"); catdec((u64)(-fr)); cat("\n"); flush(); u_exit(5); }
    if (av.has_audio) {
        i64 ar = u_audio_open(av.rate);
        if (ar == 0) audio_on = 1;
    }
    u64 offx = (sw - av.width) / 2, offy = (sh - av.height) / 2;

    voff = av.movi_start; aoff = av.movi_start;
    feed_audio(1u << 20);                        /* relleno inicial: la reproduccion arranca con el primer dato */

    u64 t_start = u_uptime_ms();
    u64 frames = 0, shown = 0, dropped = 0, bad = 0, max_late = 0, idx = 0;
    int have_frame = 0, finished = 0;
    while (!finished) {
        /* siguiente chunk de video */
        unsigned char h[8]; struct avi_chunk c; u64 here = 0; int got = 0;
        while (voff + 8 <= av.movi_end) {
            if (!read_hdr(vfd, voff, h)) { voff = av.movi_end; break; }
            avi_chunk_parse(h, &c); here = voff; voff = avi_chunk_next(voff, &c);
            if (c.kind == AVI_K_VIDEO && c.stream == av.video_stream) { got = 1; break; }
        }
        if (!got) break;
        idx = frames++;
        int last = 0;
        {   /* ¿es el ultimo fotograma? se mira si queda algun chunk de video mas */
            u64 o = voff; int more = 0;
            while (o + 8 <= av.movi_end && !more) {
                unsigned char hh[8]; struct avi_chunk cc;
                if (!read_hdr(vfd, o, hh)) break;
                avi_chunk_parse(hh, &cc); if (cc.kind == AVI_K_VIDEO && cc.stream == av.video_stream) more = 1; o = avi_chunk_next(o, &cc);
            }
            last = !more;
        }
        u64 pts = idx * av.usec_per_frame;
        for (;;) {                                          /* esperar a que llegue su hora, alimentando el audio */
            feed_audio(65536);
            u64 clk = clock_us();
            if (clk + EARLY_US >= pts) break;
            u64 wait_ms = (pts - clk - EARLY_US) / 1000;
            u_sleep(wait_ms > 4 ? 4 : (wait_ms ? wait_ms : 1));
            if (u_uptime_ms() - t_start > 3600000ULL) break;
        }
        u64 clk = clock_us();
        if (!nodrop && !last && have_frame && clk > pts + LATE_US) { dropped++; continue; }
        if (c.size && here + 8 + c.size > av.movi_end) { frames--; break; }     /* chunk cortado por el final del archivo */
        if (c.size == 0) { /* fotograma repetido (duplicado): se mantiene el anterior */ shown++; continue; }
        if (c.size > VBUF_BYTES) { bad++; continue; }
        if (u_seek(vfd, (i64)(here + 8), 0) != (i64)(here + 8) || u_read(vfd, vbuf, c.size) != (i64)c.size) { bad++; continue; }
        struct jpeg_info ji;
        int jr = jpeg_decode(vbuf, c.size, frame, av.width, (uint32_t)(MAXPIX / av.width), &ji);
        if (jr != JPEG_OK || ji.width != av.width || ji.height != av.height) { bad++; continue; }
        if (u_fb_blit(frame, av.width, av.height, offx, offy) != 0) { bad++; continue; }
        have_frame = 1; shown++;
        u64 now = clock_us();
        if (now > pts && now - pts > max_late) max_late = now - pts;
        if (last) finished = 1;
    }
    /* termina de alimentar el audio que quede */
    while (audio_on && !audio_done) { feed_audio(1u << 20); if (!audio_done) u_sleep(5); }
    u_puts("[vidplay] reproduccion terminada\n");
    if (hold) u_sleep(hold);
    u64 ua = 0;
    if (audio_on) { u64 st[3]; if (u_audio_status(st) == 0) ua = st[2]; }
    u_fb_close();
    if (audio_on) u_audio_close(1);
    u_close(vfd); if (afd >= 0) u_close(afd);
    u64 wall = u_uptime_ms() - t_start;
    cat("[vidplay] "); cat(path); cat(": "); catdec(frames); cat(" fotogramas, "); catdec(shown); cat(" mostrados, ");
    catdec(dropped); cat(" saltados, "); catdec(bad); cat(" malos, retraso max "); catdec(max_late / 1000); cat(" ms, ");
    cat("audio "); catdec(fed_bytes); cat(" bytes, huecos "); catdec(ua); cat(", "); catdec(wall); cat(" ms\n");
    flush();
    u_exit(bad ? 1 : 0);
}
