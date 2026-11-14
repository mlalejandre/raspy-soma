#ifndef SOMA_CKPTIO_H
#define SOMA_CKPTIO_H
/* PARCHE 120 (Fase 2, persistencia): punto de control (checkpoint) del VFS a prueba de cortes y de corrupcion.
 *
 * Antes: el guardado invalidaba el marcador, copiaba y escribia el marcador sin barreras (una cache de disco que reordena podia dejar el marcador
 * durable con la copia a medias), sin comprobar nada al restaurar (un checkpoint corrupto pisaba el sistema de archivos vivo) y una restauracion
 * interrumpida dejaba el VFS mezclado, mitad nuevo y mitad viejo.
 * Ahora:
 *   - guardar: marcador a cero -> barrera -> copia (calculando el CRC32 de todo) -> barrera -> marcador V3 con CRC -> barrera. Un corte deja un
 *     marcador invalido o un checkpoint completo y verificable; NUNCA uno valido a medias.
 *   - restaurar: primero se VERIFICA el CRC (si falla, el VFS vivo no se toca); despues se escribe una marca de intencion, se invalida el diario del
 *     VFS (para que no reaplique un guardado viejo), se copia y se borra la marca. Si hay un corte, `ckpt_recover` (en el arranque) rehace la
 *     restauracion entera: el resultado es el VFS anterior o el checkpoint COMPLETO, nunca una mezcla.
 * Codigo puro (sin dependencias del nucleo) para probarlo en el host con un corte tras cada escritura y con una cache volatil que reordena. */
#include "slotio.h"

#define CKPT_MAGIC3   "MYOS_CHECKPOINT_V3"
#define CKPT_MAGIC2   "MYOS_CHECKPOINT_V2"     /* formato antiguo: sin CRC; se acepta pero no se puede verificar */
#define CKPT_INTENT   "SOMA_RESTORE_PENDING"

struct ckpt_marker {
    char     magic[24];
    uint32_t seq;
    uint32_t nsec;
    uint32_t crc;           /* CRC32 de los nsec sectores copiados */
    uint32_t hdr_crc;       /* CRC32 de los 36 bytes anteriores */
    uint8_t  pad[512 - 40];
};

static inline int ckpt_streq(const char *a, const char *lit) { for (size_t i = 0; lit[i]; ++i) if (a[i] != lit[i]) return 0; return 1; }

static inline int ckpt_save(const struct slotdev *d, uint64_t src, uint64_t dst, uint32_t nsec, uint64_t marker_lba, uint32_t seq)
{
    uint8_t b[SLOTIO_SEC]; slotio_zero(b, sizeof b);
    if (d->wr(d->ctx, marker_lba, b) != 0) return -1;               /* 1. invalidar */
    if (slotio_barrier(d) != 0) return -1;
    uint32_t crc = 0;
    for (uint32_t s = 0; s < nsec; ++s) {                            /* 2. copiar */
        if (d->rd(d->ctx, src + s, b) != 0) return -1;
        crc = slotio_crc32(crc, b, sizeof b);
        if (d->wr(d->ctx, dst + s, b) != 0) return -1;
    }
    if (slotio_barrier(d) != 0) return -1;                           /* 3. la copia es durable ANTES del marcador */
    struct ckpt_marker m; slotio_zero(&m, sizeof m);
    slotio_copy(m.magic, CKPT_MAGIC3, 18);
    m.seq = seq; m.nsec = nsec; m.crc = crc; m.hdr_crc = slotio_crc32(0, &m, 36);
    if (d->wr(d->ctx, marker_lba, &m) != 0) return -1;               /* 4. marcador el ULTIMO */
    return slotio_barrier(d);
}

/* 0 = no hay checkpoint valido; 1 = formato antiguo (valido sin poder verificar el contenido); 2 = V3 con el CRC verificado. */
static inline int ckpt_valid(const struct slotdev *d, uint64_t dst, uint32_t nsec, uint64_t marker_lba)
{
    struct ckpt_marker m; uint8_t b[SLOTIO_SEC];
    if (d->rd(d->ctx, marker_lba, &m) != 0) return 0;
    if (ckpt_streq(m.magic, CKPT_MAGIC2)) { for (int i = 18; i < 24; ++i) if (m.magic[i]) return 0; return 1; }
    if (!ckpt_streq(m.magic, CKPT_MAGIC3)) return 0;
    if (m.hdr_crc != slotio_crc32(0, &m, 36) || m.nsec != nsec) return 0;
    uint32_t crc = 0;
    for (uint32_t s = 0; s < nsec; ++s) { if (d->rd(d->ctx, dst + s, b) != 0) return 0; crc = slotio_crc32(crc, b, sizeof b); }
    return crc == m.crc ? 2 : 0;
}

static inline int ckpt_copy_back(const struct slotdev *d, uint64_t dst, uint64_t live, uint32_t nsec)
{
    uint8_t b[SLOTIO_SEC];
    for (uint32_t s = 0; s < nsec; ++s) { if (d->rd(d->ctx, dst + s, b) != 0 || d->wr(d->ctx, live + s, b) != 0) return -1; }
    return slotio_barrier(d);
}

/* Restaura el checkpoint sobre el VFS vivo. -1: no hay checkpoint valido (el VFS no se toca); -2: fallo de E/S. */
static inline int ckpt_restore(const struct slotdev *d, uint64_t dst, uint64_t live, uint32_t nsec, uint64_t marker_lba, uint64_t intent_lba, uint64_t journal_lba)
{
    if (ckpt_valid(d, dst, nsec, marker_lba) == 0) return -1;
    uint8_t b[SLOTIO_SEC]; slotio_zero(b, sizeof b);
    slotio_copy(b, CKPT_INTENT, 20);
    if (d->wr(d->ctx, intent_lba, b) != 0 || slotio_barrier(d) != 0) return -2;      /* desde aqui, un corte se REHACE en el arranque */
    slotio_zero(b, sizeof b);
    if (d->wr(d->ctx, journal_lba, b) != 0 || slotio_barrier(d) != 0) return -2;     /* que un guardado viejo no se reaplique sobre lo restaurado */
    if (ckpt_copy_back(d, dst, live, nsec) != 0) return -2;
    if (d->wr(d->ctx, intent_lba, b) != 0 || slotio_barrier(d) != 0) return -2;      /* restauracion terminada */
    return 0;
}

/* Arranque, ANTES de recuperar el diario y de leer archivos. 0 = nada pendiente; 1 = restauracion interrumpida completada; 2 = marca de intencion sin
 * checkpoint valido (se borra; el VFS queda como esta). */
static inline int ckpt_recover(const struct slotdev *d, uint64_t dst, uint64_t live, uint32_t nsec, uint64_t marker_lba, uint64_t intent_lba, uint64_t journal_lba)
{
    uint8_t b[SLOTIO_SEC];
    if (d->rd(d->ctx, intent_lba, b) != 0 || !ckpt_streq((const char *)b, CKPT_INTENT)) return 0;
    int rc = 2;
    if (ckpt_valid(d, dst, nsec, marker_lba) != 0) {
        slotio_zero(b, sizeof b);
        if (d->wr(d->ctx, journal_lba, b) != 0 || slotio_barrier(d) != 0) return -2;
        if (ckpt_copy_back(d, dst, live, nsec) != 0) return -2;
        rc = 1;
    }
    slotio_zero(b, sizeof b);
    if (d->wr(d->ctx, intent_lba, b) != 0 || slotio_barrier(d) != 0) return -2;
    return rc;
}
#endif
