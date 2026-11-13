#ifndef SOMA_SLOTIO_H
#define SOMA_SLOTIO_H
/* PARCHE 112 (Fase 2, persistencia): actualizacion ATOMICA de un inodo del VFS con diario (write-ahead journal) de un solo hueco.
 *
 * Problema: antes, guardar un archivo escribia cabecera + 32 sectores de datos en su sitio. Un corte de luz o un cuelgue a mitad dejaba
 * una cabecera nueva con datos viejos (o al reves) y nadie podia notarlo. Ahora:
 *   1. se escribe en el diario la imagen completa del inodo (cabecera + sectores de datos usados) y SOLO ENTONCES su sector de compromiso;
 *   2. se aplica en su sitio;
 *   3. se invalida el diario.
 * En el arranque, `slotio_recover` rehace una actualizacion comprometida pero no terminada, y descarta una a medias (el inodo no se toco).
 * Resultado: tras cualquier corte, el inodo es COMPLETAMENTE el contenido anterior o COMPLETAMENTE el nuevo.
 * Codigo puro (sin dependencias del nucleo) para poder probarlo en el host inyectando un corte tras cada escritura. */
#include <stdint.h>
#include <stddef.h>

#define SLOTIO_SEC          512u
#define SLOTIO_DATA_MAX     32u                 /* sectores de datos por inodo */
#define SLOTIO_JMAGIC       "SOMAJRN1"

struct slotdev {
    int (*rd)(void *ctx, uint64_t lba, void *buf);      /* 0 = bien */
    int (*wr)(void *ctx, uint64_t lba, const void *buf);
    void *ctx;
    int (*flush)(void *ctx);                            /* opcional: barrera; vuelve cuando todo lo escrito antes es DURABLE */
};

static inline int slotio_barrier(const struct slotdev *d) { return d->flush ? d->flush(d->ctx) : 0; }

/* Sector de compromiso del diario (512 B). */
struct slotio_jhdr {
    char     magic[8];
    uint32_t seq;
    uint32_t slot;
    uint32_t nd;            /* sectores de datos en el diario (0..32) */
    uint32_t payload_crc;   /* CRC32 de: sector de cabecera + nd sectores de datos */
    uint32_t hdr_crc;       /* CRC32 de los 24 bytes anteriores */
    uint8_t  pad[512 - 28];
};

static inline uint32_t slotio_crc32(uint32_t crc, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc ^= b[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static inline void slotio_zero(void *p, size_t n) { uint8_t *b = (uint8_t *)p; for (size_t i = 0; i < n; ++i) b[i] = 0; }
static inline void slotio_copy(void *d, const void *s, size_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for (size_t i = 0; i < n; ++i) a[i] = b[i]; }

/* Sector `s` (0-based) de los datos de un archivo de `size` bytes, rellenado con ceros. */
static inline void slotio_data_sector(const uint8_t *data, uint32_t size, uint32_t s, uint8_t out[SLOTIO_SEC])
{
    slotio_zero(out, SLOTIO_SEC);
    uint32_t off = s * SLOTIO_SEC;
    if (data && off < size) { uint32_t t = size - off; if (t > SLOTIO_SEC) t = SLOTIO_SEC; slotio_copy(out, data + off, t); }
}

static inline uint32_t slotio_nd(uint32_t size) { return (size + SLOTIO_SEC - 1) / SLOTIO_SEC; }

static inline int slotio_clear_journal(const struct slotdev *d, uint64_t jlba)
{
    uint8_t z[SLOTIO_SEC] __attribute__((aligned(16)));
    slotio_zero(z, sizeof z);
    return d->wr(d->ctx, jlba, z);
}

/* Aplica una imagen (cabecera + datos) al inodo en su sitio: datos primero, cabecera la ultima. */
static inline int slotio_apply(const struct slotdev *d, uint64_t slot_lba, const uint8_t hdr[SLOTIO_SEC], const uint8_t *data, uint32_t size)
{
    uint8_t sec[SLOTIO_SEC] __attribute__((aligned(16)));
    for (uint32_t s = 0; s < SLOTIO_DATA_MAX; ++s) {
        slotio_data_sector(data, size, s, sec);
        if (d->wr(d->ctx, slot_lba + 1 + s, sec) != 0) return -1;
    }
    slotio_copy(sec, hdr, SLOTIO_SEC);
    return d->wr(d->ctx, slot_lba, sec);
}

/* Guardado atomico. size <= 32*512. */
static inline int slotio_commit(const struct slotdev *d, uint64_t jlba, uint64_t slot_lba, uint32_t slot,
                                const uint8_t hdr[SLOTIO_SEC], const uint8_t *data, uint32_t size, uint32_t seq)
{
    if (size > SLOTIO_DATA_MAX * SLOTIO_SEC) return -1;
    uint8_t sec[SLOTIO_SEC] __attribute__((aligned(16)));
    uint32_t nd = slotio_nd(size);
    uint32_t crc = slotio_crc32(0, hdr, SLOTIO_SEC);
    for (uint32_t s = 0; s < nd; ++s) {
        slotio_data_sector(data, size, s, sec);
        crc = slotio_crc32(crc, sec, SLOTIO_SEC);
        if (d->wr(d->ctx, jlba + 2 + s, sec) != 0) return -1;
    }
    if (d->wr(d->ctx, jlba + 1, hdr) != 0) return -1;            /* copia de la cabecera en el diario */
    if (slotio_barrier(d) != 0) return -1;                       /* la carga debe ser durable ANTES del compromiso */
    struct slotio_jhdr j; slotio_zero(&j, sizeof j);
    slotio_copy(j.magic, SLOTIO_JMAGIC, 8);
    j.seq = seq; j.slot = slot; j.nd = nd; j.payload_crc = crc;
    j.hdr_crc = slotio_crc32(0, &j, 24);
    if (d->wr(d->ctx, jlba, &j) != 0) return -1;                  /* COMPROMISO */
    if (slotio_barrier(d) != 0) return -1;                       /* el compromiso es durable antes de tocar el inodo y antes de dar el guardado por hecho */
    if (slotio_apply(d, slot_lba, hdr, data, size) != 0) return -1;
    if (slotio_barrier(d) != 0) return -1;                       /* el inodo es durable ANTES de invalidar el diario */
    return slotio_clear_journal(d, jlba);
}

/* Arranque: rehace o descarta. Devuelve 1 si rehizo una actualizacion, 0 si no habia nada, 2 si descarto una a medias, -1 error de E/S. */
static inline int slotio_recover(const struct slotdev *d, uint64_t jlba, uint64_t slots_base, uint32_t sectors_per_file, uint32_t max_slots)
{
    uint8_t jb[SLOTIO_SEC] __attribute__((aligned(16)));
    if (d->rd(d->ctx, jlba, jb) != 0) return -1;
    struct slotio_jhdr j; slotio_copy(&j, jb, sizeof j);
    int magic = 1; for (int i = 0; i < 8; ++i) if (j.magic[i] != SLOTIO_JMAGIC[i]) magic = 0;
    if (!magic) return 0;
    int ok = (slotio_crc32(0, &j, 24) == j.hdr_crc) && j.slot < max_slots && j.nd <= SLOTIO_DATA_MAX;
    uint8_t hdr[SLOTIO_SEC] __attribute__((aligned(16)));
    static uint8_t img[SLOTIO_DATA_MAX * SLOTIO_SEC];
    if (ok) {
        if (d->rd(d->ctx, jlba + 1, hdr) != 0) return -1;
        uint32_t crc = slotio_crc32(0, hdr, SLOTIO_SEC);
        for (uint32_t s = 0; s < j.nd && ok; ++s) {
            if (d->rd(d->ctx, jlba + 2 + s, img + s * SLOTIO_SEC) != 0) return -1;
            crc = slotio_crc32(crc, img + s * SLOTIO_SEC, SLOTIO_SEC);
        }
        ok = (crc == j.payload_crc);
    }
    if (!ok) { if (slotio_clear_journal(d, jlba) != 0) return -1; return 2; }
    /* El tamano real esta en la cabecera del inodo (bytes 48..51); los sectores sobrantes se rellenan con ceros. */
    uint32_t size = 0; slotio_copy(&size, hdr + 48, 4);
    if (size > j.nd * SLOTIO_SEC) size = j.nd * SLOTIO_SEC;
    if (slotio_apply(d, slots_base + (uint64_t)j.slot * sectors_per_file, hdr, img, size) != 0) return -1;
    if (slotio_barrier(d) != 0) return -1;
    if (slotio_clear_journal(d, jlba) != 0) return -1;
    return 1;
}
#endif
