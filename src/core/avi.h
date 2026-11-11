#ifndef MYOS_AVI_H
#define MYOS_AVI_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_022_VIDEO: lector del contenedor AVI (RIFF), neutro de arquitectura.
 * Soporta un flujo de video MJPG y, opcionalmente, un flujo de audio PCM (16 bits, mono o estereo).
 * Los datos de `movi` se recorren sin indice (idx1 se ignora): se leen solo las cabeceras de 8 bytes de cada chunk,
 * asi un reproductor puede saltar por el archivo con seek y abrir el mismo archivo dos veces (uno para video, otro
 * para audio) sin depender de como esten entrelazados. Robusto frente a archivos malformados: todos los tamanos se
 * comprueban contra el final de `movi`, y avi_parse nunca lee fuera de [buf, buf+len). */

#define AVI_OK            0
#define AVI_E_FORMAT    (-1)      /* no es RIFF AVI */
#define AVI_E_TRUNC     (-2)      /* las cabeceras no caben en lo leido */
#define AVI_E_DATA      (-3)      /* cabeceras incoherentes */
#define AVI_E_UNSUPPORTED (-4)    /* codec que no es MJPG, audio que no es PCM16, sin video, ... */

#define AVI_MAX_STREAMS 8

struct avi_info {
    uint32_t width, height;
    uint32_t usec_per_frame;        /* periodo de video (de strh scale/rate; si falta, de avih) */
    uint32_t total_frames;          /* segun la cabecera (orientativo) */
    int video_stream, audio_stream; /* indice del flujo (el ## del nombre de chunk); -1 si no hay */
    int has_audio;
    uint16_t channels, bits;
    uint32_t rate;                  /* Hz del audio */
    uint32_t movi_start, movi_end;  /* desplazamientos absolutos: primer chunk dentro de `movi` y final de la lista */
};

#define AVI_K_VIDEO 1               /* ##dc / ##db */
#define AVI_K_AUDIO 2               /* ##wb */
#define AVI_K_LIST  3               /* LIST (p. ej. "rec "): se entra en ella, sus 12 bytes de cabecera ya van descontados */
#define AVI_K_OTHER 4               /* JUNK, ix##, idx1, ... : se salta */

struct avi_chunk {
    uint32_t fourcc, size;
    int kind, stream;               /* stream: numero ## para video/audio, -1 si no aplica */
};

int avi_parse(const uint8_t *buf, size_t len, struct avi_info *info);

/* Interpreta una cabecera de 8 bytes de chunk. */
void avi_chunk_parse(const uint8_t h[8], struct avi_chunk *c);
/* Desplazamiento del siguiente chunk dado el desplazamiento absoluto de este (relleno a par incluido; en LIST se entra). */
uint64_t avi_chunk_next(uint64_t off, const struct avi_chunk *c);

/* Recorrido sobre un bufer completo (pruebas y herramientas): `*pos` empieza en info->movi_start. Devuelve 1 y el chunk
 * (datos en buf+*pos+8 antes de avanzar: se devuelve en *data_off), 0 al terminar, o AVI_E_DATA si un tamano se sale. */
int avi_walk(const uint8_t *buf, size_t len, const struct avi_info *info, uint64_t *pos, struct avi_chunk *c, uint64_t *data_off);

const char *avi_strerror(int code);

#endif
