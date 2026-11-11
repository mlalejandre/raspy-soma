#ifndef SOMA_TERM_CORE_H
#define SOMA_TERM_CORE_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_024_TERM: nucleo del emulador de terminal (neutro de arquitectura; se prueba en host con ASan/fuzz).
 *
 * Guarda hasta TC_LINES lineas LOGICAS con sus codigos ANSI de color (ESC [ ... m). El ajuste de linea se calcula al
 * dibujar (segun las columnas que quepan), asi redimensionar la ventana o cambiar el zoom reordena todo bien. Un caracter
 * visible es un byte ASCII o una secuencia UTF-8 de 2 bytes (acentos, enie). Las posiciones de la seleccion usan el numero
 * de linea ABSOLUTO (no cambia cuando el anillo descarta lineas viejas) y la columna de caracter visible. */
#define TC_LINES 1000
#define TC_RAW   224                 /* bytes por linea logica (con codigos ANSI) */

struct tc_line { uint16_t len, vis; char raw[TC_RAW]; };

struct tc_cell { char b[2]; uint8_t nb; uint8_t fg; uint8_t bold; };   /* fg: 0 = por defecto, 30..37 = color ANSI */

struct term_core {
    struct tc_line lines[TC_LINES];
    int first, count;                /* anillo: indice de la mas antigua y cuantas hay */
    uint32_t next_id;                /* id absoluto de la proxima linea */
    int cols;                        /* columnas por fila visual (>= 1) */
    int scroll;                      /* filas visuales ocultas por debajo (0 = al final) */
    int sel_active;
    uint32_t sa_id, sb_id; int sa_col, sb_col;   /* extremos (ancla y movil) */
};

void tc_init(struct term_core *t, int cols);
void tc_clear(struct term_core *t);
void tc_add(struct term_core *t, const char *raw);      /* anade una linea logica (si el usuario esta mirando mas arriba, la vista no salta) */
void tc_pop(struct term_core *t);                          /* quita la ultima linea (la "viva" del prompt) */
void tc_replace_last(struct term_core *t, const char *raw);
void tc_set_cols(struct term_core *t, int cols);

int  tc_rows_of(const struct term_core *t, int idx);    /* filas visuales de la linea idx (0 = la mas antigua) */
int  tc_total_rows(const struct term_core *t);
int  tc_scroll_max(const struct term_core *t, int view_rows);
void tc_scroll_by(struct term_core *t, int delta_rows, int view_rows);   /* delta > 0 = hacia arriba (lineas mas viejas) */
void tc_scroll_set(struct term_core *t, int scroll, int view_rows);

/* Que linea/subfila se ve en la fila de pantalla `row` (0 = arriba). Si todo cabe, el contenido empieza arriba y las filas
 * de debajo estan vacias (devuelve 0). */
int  tc_screen_row(const struct term_core *t, int view_rows, int row, int *idx, int *sub);
/* Celdas de la fila visual (idx, sub): devuelve cuantas (<= max, <= cols). */
int  tc_row_cells(const struct term_core *t, int idx, int sub, struct tc_cell *out, int max);
int  tc_vis_len(const struct term_core *t, int idx);

/* Barra de desplazamiento: posicion y alto del pulgar dentro de una pista de track_h pixeles. 0 si no hace falta barra. */
int  tc_thumb(const struct term_core *t, int view_rows, int track_h, int min_thumb, int *thumb_y, int *thumb_h);
int  tc_scroll_from_thumb(const struct term_core *t, int view_rows, int track_h, int min_thumb, int thumb_y);

/* Seleccion */
int  tc_hit(const struct term_core *t, int view_rows, int row, int col, int *idx, int *vcol);   /* pantalla -> (linea, columna visible) */
void tc_sel_begin(struct term_core *t, int idx, int vcol);
void tc_sel_extend(struct term_core *t, int idx, int vcol);
void tc_sel_clear(struct term_core *t);
void tc_select_word(struct term_core *t, int idx, int vcol);
void tc_select_line(struct term_core *t, int idx);
void tc_select_all(struct term_core *t);
int  tc_is_selected(const struct term_core *t, int idx, int vcol);
int  tc_sel_text(const struct term_core *t, char *out, int max);      /* texto plano seleccionado (UTF-8, '\n' entre lineas); devuelve su longitud */

/* Portapapeles del sistema (compartido por las apps) */
#define TC_CLIP_MAX 4096
void tc_clip_set(const char *s, int n);
const char *tc_clip_get(int *n);

#endif
