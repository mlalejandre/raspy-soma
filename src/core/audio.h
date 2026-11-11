#ifndef MYOS_AUDIO_H
#define MYOS_AUDIO_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_020_AUDIO: nucleo de audio, neutro de arquitectura (se prueba en host con un DMA simulado).
 *
 * Formato fijo: PCM de 16 bits, estereo, little endian (un "frame" = 4 bytes). La frecuencia la elige quien abre.
 *
 *   productor --audio_write--> FIFO (software) --audio_pump--> anillo DMA (hardware) --> altavoz
 *
 * El hardware lee el anillo en bucle sin parar. audio_pump() (la llama el hilo audiod cada pocos ms):
 *   1. lee la posicion del DMA y deduce cuantos bytes se han reproducido,
 *   2. PONE A CERO lo ya reproducido (asi, si falta dato, lo que suena es silencio y nunca un trozo viejo en bucle),
 *   3. rellena el anillo con lo que haya en la FIFO hasta `target` bytes por delante de la posicion de lectura.
 * Si llega dato cuando el DMA ya paso de donde terminaba lo colocado, es una INFRAUTILIZACION (hueco audible):
 * se cuenta y se recoloca desde la posicion actual (+ un pequeno margen). La FIFO vacia por si sola no cuenta
 * (es silencio: final de pista o pausa). */
#define AUDIO_FRAME 4u

struct audio_hw {
    void *ctx;
    uint8_t *ring;                 /* anillo DMA: memoria contigua accesible por la CPU */
    uint32_t ring_bytes;           /* multiplo de 4096 */
    int  (*start)(void *ctx, uint32_t rate);       /* programa el formato y arranca el DMA ciclico (0 = OK) */
    void (*stop)(void *ctx);
    uint32_t (*pos)(void *ctx);                    /* byte del anillo que el DMA esta leyendo ahora (0..ring_bytes-1) */
};

struct audio_stats {
    uint64_t played_bytes;         /* bytes que el DMA ha consumido desde que arranco */
    uint64_t data_bytes;           /* bytes de datos del usuario colocados en el anillo */
    uint32_t underruns;            /* huecos audibles: llego dato tarde */
    uint32_t pumps;
};

struct audio {
    struct audio_hw hw;
    uint8_t *fifo; uint32_t fifo_bytes;
    uint32_t fifo_rd, fifo_wr, fifo_fill;
    uint32_t rate, target, lead;
    int opened, started;
    uint64_t placed, played, cleaned;    /* contadores monotonos de bytes: colocado, reproducido, puesto a cero */
    uint32_t last_pos, base;             /* base: posicion del DMA cuando arranco (los contadores son relativos a ella) */
    struct audio_stats st;
};

void audio_init(struct audio *a, const struct audio_hw *hw, uint8_t *fifo, uint32_t fifo_bytes);
int  audio_open(struct audio *a, uint32_t rate);          /* 0 = OK; el DMA arranca con el primer dato */
void audio_close(struct audio *a);                         /* para el DMA y vacia la FIFO */
uint32_t audio_write(struct audio *a, const void *pcm, uint32_t n);   /* no bloquea: devuelve lo aceptado (multiplo de 4) */
void audio_pump(struct audio *a);
uint32_t audio_space(const struct audio *a);               /* bytes libres en la FIFO */
uint32_t audio_queued(const struct audio *a);              /* bytes en la FIFO sin colocar aun */
int  audio_idle(const struct audio *a);                    /* 1 si todo lo escrito ya sono (FIFO vacia y anillo vaciado) */
void audio_get_stats(const struct audio *a, struct audio_stats *out);
/* SOMA_PARCHE_022_VIDEO: reloj de audio = bytes de DATOS del usuario que ya han sonado (no cuenta el silencio de los
 * huecos). En microsegundos: media_bytes * 1e6 / (rate * 4). Es el reloj maestro del reproductor de video. */
/* SOMA_PARCHE_023_MEDIA: descarta lo que aun esta en la FIFO (no colocado en el anillo) y devuelve cuantos bytes eran: sirve
 * para pausar y para saltar de posicion sin esperar a que suene. Lo ya colocado en el anillo (<= ~180 ms) sigue sonando. */
uint32_t audio_flush(struct audio *a);
uint64_t audio_media_bytes(const struct audio *a);
uint64_t audio_media_us(const struct audio *a);

#endif
