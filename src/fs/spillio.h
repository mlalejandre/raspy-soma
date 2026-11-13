#ifndef SOMA_SPILLIO_H
#define SOMA_SPILLIO_H
/* PARCHE 113 (Fase 2, persistencia): actualizacion ATOMICA de un archivo grande de RamFS guardado en BIGFS.
 *
 * Problema: `fs_spill_put` hacia truncate(0) y reescribia el archivo en el sitio. Un corte despues del truncate (o entre la cabecera y los datos)
 * destruia el contenido anterior, y al arrancar el archivo desaparecia. Ahora:
 *   1. se escribe el contenido NUEVO completo en un archivo temporal (cabecera primero, datos despues; el tamano del inodo solo es el final si todo llego);
 *   2. se borra el antiguo y se renombra el temporal a su sitio (cada paso es UNA escritura atomica de sector en BIGFS).
 * En el montaje, `spill_recover` termina o descarta lo que quedo a medias: si falta el definitivo y el temporal esta completo, lo promociona;
 * si no, borra el temporal. Nunca queda un archivo a medias ni se pierde el anterior antes de tener el nuevo completo.
 * Coste: durante la actualizacion hacen falta clusteres para el viejo y el nuevo a la vez (si no caben, la escritura falla y el viejo queda intacto). */
#include <stdint.h>
#include <stddef.h>
#include "bigfs.h"

#define SPILL_HDR 64u

static inline void spill_hdr_build(uint8_t h[SPILL_HDR], const char *name, uint32_t name_max, uint32_t len)
{
    for (uint32_t i = 0; i < SPILL_HDR; ++i) h[i] = 0;
    h[0] = 'R'; h[1] = 'F'; h[2] = 'S'; h[3] = '1';
    for (uint32_t i = 0; i < name_max - 1 && name[i]; ++i) h[4 + i] = (uint8_t)name[i];
    h[52] = (uint8_t)len; h[53] = (uint8_t)(len >> 8); h[54] = (uint8_t)(len >> 16); h[55] = (uint8_t)(len >> 24);
}

/* Un archivo spill es valido si tiene la marca y su tamano real es cabecera + longitud declarada. */
static inline int spill_valid(struct bigfs *fs, const char *path, uint32_t *len_out)
{
    uint32_t ino; struct bigfs_stat st; uint8_t h[SPILL_HDR];
    if (bigfs_lookup(fs, path, &ino) < 0 || bigfs_stat(fs, ino, &st) != 0 || st.type != BIGFS_T_FILE) return 0;
    if (st.size < SPILL_HDR || bigfs_read(fs, ino, 0, h, SPILL_HDR) != (int64_t)SPILL_HDR) return 0;
    if (h[0] != 'R' || h[1] != 'F' || h[2] != 'S' || h[3] != '1') return 0;
    uint32_t len = (uint32_t)h[52] | ((uint32_t)h[53] << 8) | ((uint32_t)h[54] << 16) | ((uint32_t)h[55] << 24);
    if (st.size != (uint64_t)SPILL_HDR + len) return 0;
    if (len_out) *len_out = len;
    return 1;
}

static inline int spill_put_atomic(struct bigfs *fs, const char *slot_path, const char *tmp_path, const char *name, uint32_t name_max,
                                   const char *data, uint32_t len)
{
    uint32_t ino;
    if (bigfs_lookup(fs, tmp_path, &ino) >= 0) (void)bigfs_unlink(fs, tmp_path);          /* resto de un intento anterior */
    if (bigfs_create(fs, tmp_path, &ino) != 0) return -1;
    uint8_t h[SPILL_HDR]; spill_hdr_build(h, name, name_max, len);
    if (bigfs_write(fs, ino, 0, h, SPILL_HDR) != (int64_t)SPILL_HDR ||
        (len && bigfs_write(fs, ino, SPILL_HDR, data, len) != (int64_t)len)) {
        (void)bigfs_unlink(fs, tmp_path);                                                   /* el anterior sigue intacto */
        return -1;
    }
    uint32_t old;
    if (bigfs_lookup(fs, slot_path, &old) >= 0 && bigfs_unlink(fs, slot_path) != 0) { (void)bigfs_unlink(fs, tmp_path); return -1; }
    if (bigfs_rename(fs, tmp_path, slot_path) != 0) return -1;     /* el temporal completo queda para spill_recover */
    return 0;
}

/* Montaje: devuelve 1 si promociono un temporal, 2 si lo descarto, 0 si no habia nada. */
static inline int spill_recover(struct bigfs *fs, const char *slot_path, const char *tmp_path)
{
    uint32_t ino;
    if (bigfs_lookup(fs, tmp_path, &ino) < 0) return 0;
    uint32_t dummy;
    int slot_exists = bigfs_lookup(fs, slot_path, &dummy) >= 0;
    if (!slot_exists && spill_valid(fs, tmp_path, 0) && bigfs_rename(fs, tmp_path, slot_path) == 0) return 1;
    (void)bigfs_unlink(fs, tmp_path);
    return 2;
}
#endif
