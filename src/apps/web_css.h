#ifndef SOMA_WEB_CSS_H
#define SOMA_WEB_CSS_H
#include "web_dom.h"

enum { LK_AUTO = 0, LK_VAL = 1, LK_NONE = 2, LK_FR = 3 };
struct wlen { int32_t px; int32_t pct; uint8_t kind; };      /* pct en 1/100 % ; fr: px = fr*100 */

enum { D_NONE = 0, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_LIST_ITEM, D_FLEX, D_INLINE_FLEX, D_TABLE, D_TABLE_ROW,
       D_TABLE_CELL, D_TABLE_GROUP, D_TABLE_CAPTION, D_GRID, D_CONTENTS, D_INLINE_TABLE };
enum { P_STATIC = 0, P_RELATIVE, P_ABSOLUTE, P_FIXED, P_STICKY };
enum { FL_NONE = 0, FL_LEFT, FL_RIGHT };
enum { TA_LEFT = 0, TA_CENTER, TA_RIGHT, TA_JUSTIFY };
enum { WS_NORMAL = 0, WS_NOWRAP, WS_PRE, WS_PRE_WRAP, WS_PRE_LINE };
enum { VA_BASELINE = 0, VA_TOP, VA_MIDDLE, VA_BOTTOM, VA_SUB, VA_SUPER, VA_TEXT_TOP, VA_TEXT_BOTTOM };
enum { LS_DISC = 0, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_NONE, LS_ALPHA, LS_ROMAN, LS_UALPHA };
enum { JC_START = 0, JC_END, JC_CENTER, JC_BETWEEN, JC_AROUND, JC_EVENLY };
enum { AI_STRETCH = 0, AI_START, AI_END, AI_CENTER, AI_BASELINE, AI_AUTO };

struct wvar { struct wvar *next; const char *name; const char *val; };

struct wstyle {
    uint8_t display, position, floating, clear, visibility, overflow_x, overflow_y, white_space, text_align;
    uint8_t text_decor;           /* bit0 underline, bit1 line-through, bit2 overline */
    uint8_t text_transform;       /* 0 none, 1 upper, 2 lower, 3 capitalize */
    uint8_t vertical_align, italic, bold, mono, list_style, list_inside, box_sizing;
    uint8_t flex_dir;             /* 0 row, 1 row-rev, 2 col, 3 col-rev */
    uint8_t flex_wrap, justify, align_items, align_self, align_content;
    uint8_t border_style[4];      /* 0 none, 1 solid, 2 dashed, 3 dotted */
    uint8_t border_collapse, text_overflow, word_break, bg_repeat, bg_size_mode, opacity;
    uint8_t grad_dir, has_grad, pointer, clearfix;
    int16_t font_px;
    uint8_t lh_mode;              /* 0 normal, 1 multiplicador (lh_val x100), 2 px */
    int16_t lh_val;
    int16_t border_w[4];          /* top right bottom left */
    int16_t radius, letter_sp, z_index, order, spacing_h, spacing_v;
    int16_t flex_grow, flex_shrink;   /* x100 */
    int16_t grid_cols, col_span, bg_pos_x, bg_pos_y;
    uint32_t color, bg_color, border_color[4], grad_a, grad_b;
    struct wlen width, height, min_w, max_w, min_h, max_h;
    struct wlen margin[4], padding[4];
    struct wlen top, right, bottom, left, flex_basis, gap_r, gap_c, text_indent;
    struct wlen bg_w, bg_h;
    struct wlen grid_tracks[8];
    const char *bg_image;
    struct wvar *vars;
    /* SOMA_PARCHE_028_UI */
    uint32_t shadow_color;        /* box-shadow exterior (0 = sin sombra) */
    int16_t shadow_x, shadow_y, shadow_blur, shadow_spread;
    int16_t grid_auto_min;        /* repeat(auto-fit|auto-fill, minmax(N, 1fr)): ancho minimo de pista; las columnas se cuentan al maquetar */
};

struct wcss;                                   /* contexto de estilos del documento */
struct wcss *wc_new(struct wdoc *d);
void wc_free(struct wcss *c);
/* anade una hoja (origin 0 = UA, 1 = autor) */
void wc_add_sheet(struct wcss *c, const char *css, uint32_t len, int origin);
/* @import pendientes: devuelve la siguiente URL pendiente o NULL */
const char *wc_next_import(struct wcss *c);
/* calcula el estilo de todo el arbol para un viewport */
void wc_compute(struct wcss *c, int vw, int vh);
/* devuelve la primera coincidencia / llena un array. */
int wc_query(struct wcss *c, struct wnode *root, const char *sel, struct wnode **out, int max, int first_only);
int wc_matches(struct wcss *c, struct wnode *n, const char *sel);
/* estilo en linea */
void wc_set_inline(struct wdoc *d, struct wnode *n, const char *prop, const char *val);
const char *wc_get_inline(struct wnode *n, const char *prop, char *out, uint32_t max);
uint32_t wc_parse_color(const char *s, int *ok);
int wc_line_px(const struct wstyle *s);
int wc_parse_length_px(const char *s, int base);   /* "12px", "1.5em" -> px (em = 16) */
#endif
