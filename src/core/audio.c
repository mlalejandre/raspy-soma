/* SOMA_PARCHE_020_AUDIO: ver audio.h */
#include "audio.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

void audio_init(struct audio *a, const struct audio_hw *hw, uint8_t *fifo, uint32_t fifo_bytes)
{
    uint8_t *p = (uint8_t *)a;
    for (size_t i = 0; i < sizeof(*a); ++i) p[i] = 0;
    a->hw = *hw;
    a->fifo = fifo; a->fifo_bytes = fifo_bytes & ~(AUDIO_FRAME - 1);
}

static void zero_ring(struct audio *a, uint64_t from, uint64_t to)       /* [from, to) en contadores monotonos */
{
    while (from < to) {
        uint32_t off = (uint32_t)((a->base + from) % a->hw.ring_bytes);
        uint32_t n = (uint32_t)MIN(to - from, (uint64_t)(a->hw.ring_bytes - off));
        for (uint32_t i = 0; i < n; ++i) a->hw.ring[off + i] = 0;
        from += n;
    }
}

int audio_open(struct audio *a, uint32_t rate)
{
    if (a->opened || rate < 4000 || rate > 192000) return -1;
    if (!a->hw.ring || a->hw.ring_bytes < 8192 || (a->hw.ring_bytes % 4096)) return -2;
    a->base = 0;
    zero_ring(a, 0, a->hw.ring_bytes);
    a->rate = rate;
    a->fifo_rd = a->fifo_wr = a->fifo_fill = 0;
    a->placed = a->played = a->cleaned = 0; a->last_pos = 0; a->started = 0;
    /* por delante de la posicion de lectura se mantienen ~180 ms (como mucho 1/4 del anillo) */
    uint32_t t = rate * AUDIO_FRAME / 5;
    if (t > a->hw.ring_bytes / 4) t = a->hw.ring_bytes / 4;
    a->target = t & ~(AUDIO_FRAME - 1);
    a->lead = (rate / 500) * AUDIO_FRAME;                 /* ~2 ms de margen al recolocar tras un hueco */
    a->st.played_bytes = a->st.data_bytes = 0; a->st.underruns = 0; a->st.pumps = 0;
    a->opened = 1;
    return 0;
}

void audio_close(struct audio *a)
{
    if (!a->opened) return;
    if (a->started && a->hw.stop) a->hw.stop(a->hw.ctx);
    a->started = 0; a->opened = 0;
    a->fifo_rd = a->fifo_wr = a->fifo_fill = 0;
}

uint32_t audio_space(const struct audio *a) { return a->fifo_bytes - a->fifo_fill; }
uint32_t audio_queued(const struct audio *a) { return a->fifo_fill; }

uint32_t audio_write(struct audio *a, const void *pcm, uint32_t n)
{
    if (!a->opened || !pcm) return 0;
    n &= ~(AUDIO_FRAME - 1);
    n = MIN(n, audio_space(a));
    const uint8_t *s = (const uint8_t *)pcm;
    for (uint32_t i = 0; i < n; ++i) {
        a->fifo[a->fifo_wr] = s[i];
        if (++a->fifo_wr == a->fifo_bytes) a->fifo_wr = 0;
    }
    a->fifo_fill += n;
    return n;
}

int audio_idle(const struct audio *a)
{
    return a->fifo_fill == 0 && (!a->started || a->played >= a->placed);
}

void audio_pump(struct audio *a)
{
    if (!a->opened) return;
    a->st.pumps++;

    if (!a->started) {
        if (a->fifo_fill == 0) return;                    /* el DMA arranca con el primer dato: no hay "hueco" inicial */
        if (a->hw.start(a->hw.ctx, a->rate) != 0) return;
        a->started = 1;
        a->last_pos = (a->hw.pos(a->hw.ctx) % a->hw.ring_bytes) & ~(AUDIO_FRAME - 1);
        a->base = a->last_pos;                            /* los contadores cuentan desde donde esta el DMA ahora */
        a->played = 0; a->cleaned = 0;
        a->placed = a->lead;                              /* margen: el DMA ya esta leyendo */
    } else {
        /* 1) cuanto avanzo el DMA desde la ultima vez (supone < una vuelta del anillo entre dos llamadas) */
        uint32_t pos = a->hw.pos(a->hw.ctx) % a->hw.ring_bytes;
        pos &= ~(AUDIO_FRAME - 1);
        uint32_t delta = (pos >= a->last_pos) ? pos - a->last_pos : pos + a->hw.ring_bytes - a->last_pos;
        a->played += delta; a->last_pos = pos;
        a->st.played_bytes = a->played;
        /* 2) lo ya reproducido se pone a cero: lo que no se rellene suena como silencio, nunca como eco */
        if (a->played > a->cleaned) {
            /* TODO lo reproducido desde la ultima vez (menos de una vuelta, por hipotesis) se pone a cero. No se
             * limita: lo que hay por delante de la posicion de lectura aun no se ha colocado nada (placed <= played
             * tras avanzar el DMA mas que el margen), y dejar datos viejos sonaria como eco en la vuelta siguiente */
            zero_ring(a, a->cleaned, a->played); a->cleaned = a->played;
        }
    }

    /* 3) hueco audible: hay dato nuevo y el DMA ya paso de donde acababa lo colocado */
    if (a->fifo_fill > 0 && a->played >= a->placed) {
        a->st.underruns++;
        a->placed = a->played + a->lead;
    }

    /* 4) rellenar hasta `target` bytes por delante de la posicion de lectura */
    while (a->fifo_fill > 0 && a->placed < a->played + a->target) {
        uint64_t room = a->played + a->target - a->placed;
        uint32_t off = (uint32_t)((a->base + a->placed) % a->hw.ring_bytes);
        uint32_t n = (uint32_t)MIN((uint64_t)a->fifo_fill, room);
        n = MIN(n, a->hw.ring_bytes - off);                /* sin cruzar el final del anillo */
        n = MIN(n, a->fifo_bytes - a->fifo_rd);            /* ni el de la FIFO */
        n &= ~(AUDIO_FRAME - 1);
        if (n == 0) break;
        for (uint32_t i = 0; i < n; ++i) a->hw.ring[off + i] = a->fifo[a->fifo_rd + i];
        a->fifo_rd += n; if (a->fifo_rd == a->fifo_bytes) a->fifo_rd = 0;
        a->fifo_fill -= n;
        a->placed += n;
        a->st.data_bytes += n;
    }
}

void audio_get_stats(const struct audio *a, struct audio_stats *out) { *out = a->st; }

/* SOMA_PARCHE_022_VIDEO */
uint64_t audio_media_bytes(const struct audio *a)
{
    if (!a->opened || !a->started) return 0;
    uint64_t unplayed = a->placed > a->played ? a->placed - a->played : 0;      /* lo colocado que aun no ha sonado */
    return a->st.data_bytes > unplayed ? a->st.data_bytes - unplayed : 0;
}

uint64_t audio_media_us(const struct audio *a)
{
    if (!a->rate) return 0;
    return audio_media_bytes(a) * 1000000ULL / ((uint64_t)a->rate * AUDIO_FRAME);
}

/* SOMA_PARCHE_023_MEDIA */
uint32_t audio_flush(struct audio *a)
{
    if (!a->opened) return 0;
    uint32_t n = a->fifo_fill;
    a->fifo_rd = a->fifo_wr = a->fifo_fill = 0;
    return n;
}
