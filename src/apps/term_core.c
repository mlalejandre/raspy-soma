/* SOMA_PARCHE_024_TERM: ver term_core.h */
#include "term_core.h"

static char g_clip[TC_CLIP_MAX + 1];
static int g_clip_n;

extern int clip_set_text(const char *, uint32_t) __attribute__((weak));
extern const char *clip_text(uint32_t *) __attribute__((weak));
extern uint32_t clip_seq(void) __attribute__((weak));
static uint32_t g_seen;

void tc_clip_set(const char *s, int n)
{
    if (n < 0) n = 0;
    if (n > TC_CLIP_MAX) n = TC_CLIP_MAX;
    for (int i = 0; i < n; ++i) g_clip[i] = s[i];
    g_clip[n] = 0; g_clip_n = n;
    /* SOMA_PARCHE_PORTAPAPELES: compartir con el portapapeles global */
    if (clip_set_text && n > 0) { clip_set_text(g_clip, (uint32_t)n); g_seen = clip_seq(); }
}
const char *tc_clip_get(int *n)
{
    if (clip_seq && clip_text && clip_seq() != g_seen) {      /* algo se copio en otra aplicacion */
        uint32_t m = 0; const char *t = clip_text(&m); g_seen = clip_seq();
        if (t && m) { if (m > TC_CLIP_MAX) m = TC_CLIP_MAX; for (uint32_t i = 0; i < m; ++i) g_clip[i] = t[i]; g_clip[m] = 0; g_clip_n = (int)m; }
    }
    if (n) *n = g_clip_n;
    return g_clip;
}

/* ---- recorrido de caracteres visibles de una linea cruda ---- */
struct walk { const char *r; int n, i; uint8_t fg, bold; };

static int walk_next(struct walk *w, struct tc_cell *c)
{
    while (w->i < w->n) {
        uint8_t ch = (uint8_t)w->r[w->i];
        if (ch == 27 && w->i + 1 < w->n && w->r[w->i + 1] == '[') {
            int j = w->i + 2;
            while (j < w->n && ((w->r[j] >= '0' && w->r[j] <= '9') || w->r[j] == ';')) j++;
            if (j < w->n && w->r[j] == 'm') {                      /* SGR: ESC [ n ; n ; ... m */
                int num = 0, have = 0;
                for (int k = w->i + 2; k <= j; ++k) {
                    char q = (k < j) ? w->r[k] : ';';
                    if (q == ';') {
                        int v = have ? num : 0;
                        if (v == 0) { w->fg = 0; w->bold = 0; }
                        else if (v == 1) w->bold = 1;
                        else if (v >= 30 && v <= 37) w->fg = (uint8_t)v;
                        else if (v == 39) w->fg = 0;
                        num = 0; have = 0;
                    } else { num = num * 10 + (q - '0'); have = 1; if (num > 1000) num = 1000; }
                }
            }
            w->i = (j < w->n) ? j + 1 : w->n;                      /* salta la secuencia (completa o cortada) */
            continue;
        }
        c->fg = w->fg; c->bold = w->bold;
        if (ch >= 0xC2 && ch <= 0xDF && w->i + 1 < w->n && ((uint8_t)w->r[w->i + 1] & 0xC0) == 0x80) {
            c->b[0] = (char)ch; c->b[1] = w->r[w->i + 1]; c->nb = 2; w->i += 2;
        } else {
            c->b[0] = (ch < 32 || ch == 127 || ch >= 0x80) ? (ch == '\t' ? ' ' : (ch >= 0x80 ? '?' : ' ')) : (char)ch;
            c->b[1] = 0; c->nb = 1; w->i += 1;
        }
        return 1;
    }
    return 0;
}

static void walk_init(struct walk *w, const struct tc_line *l) { w->r = l->raw; w->n = l->len; w->i = 0; w->fg = 0; w->bold = 0; }

static int count_vis(const char *raw, int len)
{
    struct walk w; w.r = raw; w.n = len; w.i = 0; w.fg = 0; w.bold = 0;
    struct tc_cell c; int n = 0;
    while (walk_next(&w, &c)) n++;
    return n;
}

/* ---- anillo ---- */
static struct tc_line *line_at(const struct term_core *t, int idx) { return (struct tc_line *)&t->lines[(t->first + idx) % TC_LINES]; }

void tc_init(struct term_core *t, int cols)
{
    t->first = 0; t->count = 0; t->next_id = 0; t->cols = cols < 1 ? 1 : cols; t->scroll = 0; t->sel_active = 0;
    t->sa_id = t->sb_id = 0; t->sa_col = t->sb_col = 0;
}

void tc_clear(struct term_core *t)
{
    t->first = 0; t->count = 0; t->scroll = 0; t->sel_active = 0;      /* los ids siguen creciendo */
}

int tc_vis_len(const struct term_core *t, int idx) { return (idx >= 0 && idx < t->count) ? line_at(t, idx)->vis : 0; }

int tc_rows_of(const struct term_core *t, int idx)
{
    int v = tc_vis_len(t, idx);
    if (v <= 0) return 1;
    return (v + t->cols - 1) / t->cols;
}

int tc_total_rows(const struct term_core *t)
{
    int s = 0;
    for (int i = 0; i < t->count; ++i) s += tc_rows_of(t, i);
    return s;
}

void tc_add(struct term_core *t, const char *raw)
{
    int n = 0;
    while (raw && raw[n] && n < TC_RAW - 1) n++;
    struct tc_line *l;
    if (t->count == TC_LINES) {                       /* anillo lleno: se descarta la mas antigua */
        int dropped = tc_rows_of(t, 0);
        t->first = (t->first + 1) % TC_LINES; t->count--;
        if (t->scroll > 0) { t->scroll -= dropped; if (t->scroll < 0) t->scroll = 0; }
    }
    l = line_at(t, t->count);
    for (int i = 0; i < n; ++i) l->raw[i] = raw[i];
    l->raw[n] = 0; l->len = (uint16_t)n; l->vis = (uint16_t)count_vis(l->raw, n);
    t->count++; t->next_id++;
    if (t->scroll > 0) t->scroll += tc_rows_of(t, t->count - 1);   /* la vista no salta mientras el usuario mira mas arriba */
    if (t->count == 0) t->scroll = 0;
}

void tc_pop(struct term_core *t)
{
    if (t->count == 0) return;
    t->count--; t->next_id--;
    if (t->count == 0) t->scroll = 0;
}

void tc_replace_last(struct term_core *t, const char *raw)
{
    if (t->count == 0) { tc_add(t, raw); return; }
    int before = tc_rows_of(t, t->count - 1);
    int n = 0;
    while (raw && raw[n] && n < TC_RAW - 1) n++;
    struct tc_line *l = line_at(t, t->count - 1);
    for (int i = 0; i < n; ++i) l->raw[i] = raw[i];
    l->raw[n] = 0; l->len = (uint16_t)n; l->vis = (uint16_t)count_vis(l->raw, n);
    if (t->scroll > 0) t->scroll += tc_rows_of(t, t->count - 1) - before;     /* la vista no salta */
    if (t->scroll < 0) t->scroll = 0;
}

void tc_set_cols(struct term_core *t, int cols)
{
    if (cols < 1) cols = 1;
    if (cols == t->cols) return;
    t->cols = cols;
    int tr = tc_total_rows(t);
    if (t->scroll > tr) t->scroll = tr;
}

int tc_scroll_max(const struct term_core *t, int view_rows)
{
    int m = tc_total_rows(t) - view_rows;
    return m > 0 ? m : 0;
}

void tc_scroll_set(struct term_core *t, int scroll, int view_rows)
{
    int m = tc_scroll_max(t, view_rows);
    if (scroll < 0) scroll = 0;
    if (scroll > m) scroll = m;
    t->scroll = scroll;
}

void tc_scroll_by(struct term_core *t, int delta, int view_rows) { tc_scroll_set(t, t->scroll + delta, view_rows); }

int tc_screen_row(const struct term_core *t, int view_rows, int row, int *idx, int *sub)
{
    if (row < 0 || row >= view_rows) return 0;
    int total = tc_total_rows(t);
    int g;
    if (total <= view_rows) g = row;                   // cabe todo: el contenido empieza arriba
    else g = (total - 1 - t->scroll) - (view_rows - 1) + row;   // si no, se cuenta desde la ultima fila visible
    if (g < 0 || g >= total) return 0;
    int acc = 0;
    for (int i = 0; i < t->count; ++i) {
        int r = tc_rows_of(t, i);
        if (g < acc + r) { *idx = i; *sub = g - acc; return 1; }
        acc += r;
    }
    return 0;
}

int tc_row_cells(const struct term_core *t, int idx, int sub, struct tc_cell *out, int max)
{
    if (idx < 0 || idx >= t->count || sub < 0) return 0;
    const struct tc_line *l = line_at(t, idx);
    struct walk w; walk_init(&w, l);
    struct tc_cell c; int pos = 0, n = 0, start = sub * t->cols, end = start + t->cols;
    while (walk_next(&w, &c)) {
        if (pos >= end) break;
        if (pos >= start && n < max) out[n++] = c;
        pos++;
    }
    return n;
}

int tc_thumb(const struct term_core *t, int view_rows, int track_h, int min_thumb, int *thumb_y, int *thumb_h)
{
    int total = tc_total_rows(t);
    if (total <= view_rows || track_h <= 0 || view_rows <= 0) return 0;
    int h = (int)((int64_t)track_h * view_rows / total);
    if (h < min_thumb) h = min_thumb;
    if (h > track_h) h = track_h;
    int range = track_h - h, maxs = total - view_rows;
    int from_top = maxs - (t->scroll > maxs ? maxs : t->scroll);
    int y = maxs > 0 ? (int)((int64_t)range * from_top / maxs) : 0;
    *thumb_y = y; *thumb_h = h;
    return 1;
}

int tc_scroll_from_thumb(const struct term_core *t, int view_rows, int track_h, int min_thumb, int thumb_y)
{
    int ty, th;
    if (!tc_thumb(t, view_rows, track_h, min_thumb, &ty, &th)) return 0;
    int range = track_h - th, maxs = tc_total_rows(t) - view_rows;
    if (thumb_y < 0) thumb_y = 0;
    if (thumb_y > range) thumb_y = range;
    int from_top = range > 0 ? (int)((int64_t)thumb_y * maxs / range) : 0;
    return maxs - from_top;
}

/* ---- seleccion ---- */
static int id_to_idx(const struct term_core *t, uint32_t id)
{
    uint32_t oldest = t->next_id - (uint32_t)t->count;
    if (id < oldest) return -1;
    uint32_t i = id - oldest;
    return i < (uint32_t)t->count ? (int)i : -2;
}
static uint32_t idx_to_id(const struct term_core *t, int idx) { return t->next_id - (uint32_t)t->count + (uint32_t)idx; }

int tc_hit(const struct term_core *t, int view_rows, int row, int col, int *idx, int *vcol)
{
    int i, sub;
    if (!tc_screen_row(t, view_rows, row, &i, &sub)) return 0;
    int v = tc_vis_len(t, i);
    int c = sub * t->cols + (col < 0 ? 0 : col);
    if (c > v) c = v;
    *idx = i; *vcol = c;
    return 1;
}

void tc_sel_begin(struct term_core *t, int idx, int vcol)
{
    t->sa_id = t->sb_id = idx_to_id(t, idx); t->sa_col = t->sb_col = vcol; t->sel_active = 1;
}
void tc_sel_extend(struct term_core *t, int idx, int vcol)
{
    if (!t->sel_active) { tc_sel_begin(t, idx, vcol); return; }
    t->sb_id = idx_to_id(t, idx); t->sb_col = vcol;
}
void tc_sel_clear(struct term_core *t) { t->sel_active = 0; }

static void ordered(const struct term_core *t, uint32_t *i0, int *c0, uint32_t *i1, int *c1)
{
    int swap = (t->sa_id > t->sb_id) || (t->sa_id == t->sb_id && t->sa_col > t->sb_col);
    if (swap) { *i0 = t->sb_id; *c0 = t->sb_col; *i1 = t->sa_id; *c1 = t->sa_col; }
    else { *i0 = t->sa_id; *c0 = t->sa_col; *i1 = t->sb_id; *c1 = t->sb_col; }
}

static int is_word_char(const struct tc_cell *c)
{
    if (c->nb == 2) return 1;
    char x = c->b[0];
    return (x >= '0' && x <= '9') || (x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || x == '_' || x == '-' || x == '.' || x == '/' || x == ':' || x == '~' || x == '@';
}

void tc_select_word(struct term_core *t, int idx, int vcol)
{
    if (idx < 0 || idx >= t->count) return;
    const struct tc_line *l = line_at(t, idx);
    int n = l->vis;
    if (n == 0) return;
    if (vcol >= n) vcol = n - 1;
    if (vcol < 0) vcol = 0;
    struct walk w; walk_init(&w, l);
    struct tc_cell c; int pos = 0, a = vcol, b = vcol, cls = -1;
    static struct tc_cell cells[TC_RAW];
    while (walk_next(&w, &c) && pos < TC_RAW) cells[pos++] = c;
    cls = is_word_char(&cells[vcol]);
    while (a > 0 && is_word_char(&cells[a - 1]) == cls) a--;
    while (b < n - 1 && is_word_char(&cells[b + 1]) == cls) b++;
    t->sa_id = t->sb_id = idx_to_id(t, idx); t->sa_col = a; t->sb_col = b + 1 > n ? n : b; t->sel_active = 1;
}

void tc_select_line(struct term_core *t, int idx)
{
    if (idx < 0 || idx >= t->count) return;
    int n = tc_vis_len(t, idx);
    t->sa_id = t->sb_id = idx_to_id(t, idx); t->sa_col = 0; t->sb_col = n > 0 ? n - 1 : 0; t->sel_active = n > 0;
}

void tc_select_all(struct term_core *t)
{
    if (t->count == 0) { t->sel_active = 0; return; }
    t->sa_id = idx_to_id(t, 0); t->sa_col = 0;
    t->sb_id = idx_to_id(t, t->count - 1);
    int n = tc_vis_len(t, t->count - 1); t->sb_col = n > 0 ? n - 1 : 0;
    t->sel_active = 1;
}

int tc_is_selected(const struct term_core *t, int idx, int vcol)
{
    if (!t->sel_active || idx < 0 || idx >= t->count) return 0;
    uint32_t i0, i1; int c0, c1;
    ordered(t, &i0, &c0, &i1, &c1);
    uint32_t id = idx_to_id(t, idx);
    if (id < i0 || id > i1) return 0;
    if (id == i0 && vcol < c0) return 0;
    if (id == i1 && vcol > c1) return 0;
    return vcol < tc_vis_len(t, idx);
}

int tc_sel_text(const struct term_core *t, char *out, int max)
{
    if (!t->sel_active || max <= 0) { if (max > 0) out[0] = 0; return 0; }
    uint32_t i0, i1; int c0, c1;
    ordered(t, &i0, &c0, &i1, &c1);
    int n = 0;
    for (uint32_t id = i0; id <= i1; ++id) {
        int idx = id_to_idx(t, id);
        if (idx < 0) continue;
        if (idx == -2) break;
        const struct tc_line *l = line_at(t, idx);
        struct walk w; walk_init(&w, l);
        struct tc_cell c; int pos = 0;
        int from = (id == i0) ? c0 : 0, to = (id == i1) ? c1 : 0x7FFFFFFF;
        int line_start = n;
        while (walk_next(&w, &c)) {
            if (pos >= from && pos <= to) {
                for (int k = 0; k < c.nb; ++k) if (n < max - 1) out[n++] = c.b[k];
            }
            pos++;
        }
        while (n > line_start && out[n - 1] == ' ' && id != i1) n--;       /* sin espacios sobrantes al final de cada linea */
        if (id != i1 && n < max - 1) out[n++] = '\n';
        if (id == 0xFFFFFFFFu) break;
    }
    out[n] = 0;
    return n;
}
