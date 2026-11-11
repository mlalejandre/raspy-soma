/* SOMA_PARCHE_NAVEGADOR: motor CSS (analizador, selectores, cascada, propiedades calculadas). Solo enteros. */
#include "web_css.h"

/* ============================ colores ============================ */
static const char color_names[] =
 "aliceblue f0f8ff antiquewhite faebd7 aqua 00ffff aquamarine 7fffd4 azure f0ffff beige f5f5dc bisque ffe4c4 black 000000 blanchedalmond ffebcd blue 0000ff blueviolet 8a2be2 brown a52a2a burlywood deb887 cadetblue 5f9ea0 chartreuse 7fff00 chocolate d2691e coral ff7f50 cornflowerblue 6495ed cornsilk fff8dc crimson dc143c cyan 00ffff "
 "darkblue 00008b darkcyan 008b8b darkgoldenrod b8860b darkgray a9a9a9 darkgrey a9a9a9 darkgreen 006400 darkkhaki bdb76b darkmagenta 8b008b darkolivegreen 556b2f darkorange ff8c00 darkorchid 9932cc darkred 8b0000 darksalmon e9967a darkseagreen 8fbc8f darkslateblue 483d8b darkslategray 2f4f4f darkslategrey 2f4f4f darkturquoise 00ced1 darkviolet 9400d3 deeppink ff1493 deepskyblue 00bfff dimgray 696969 dimgrey 696969 dodgerblue 1e90ff "
 "firebrick b22222 floralwhite fffaf0 forestgreen 228b22 fuchsia ff00ff gainsboro dcdcdc ghostwhite f8f8ff gold ffd700 goldenrod daa520 gray 808080 grey 808080 green 008000 greenyellow adff2f honeydew f0fff0 hotpink ff69b4 indianred cd5c5c indigo 4b0082 ivory fffff0 khaki f0e68c "
 "lavender e6e6fa lavenderblush fff0f5 lawngreen 7cfc00 lemonchiffon fffacd lightblue add8e6 lightcoral f08080 lightcyan e0ffff lightgoldenrodyellow fafad2 lightgray d3d3d3 lightgrey d3d3d3 lightgreen 90ee90 lightpink ffb6c1 lightsalmon ffa07a lightseagreen 20b2aa lightskyblue 87cefa lightslategray 778899 lightslategrey 778899 lightsteelblue b0c4de lightyellow ffffe0 lime 00ff00 limegreen 32cd32 linen faf0e6 "
 "magenta ff00ff maroon 800000 mediumaquamarine 66cdaa mediumblue 0000cd mediumorchid ba55d3 mediumpurple 9370db mediumseagreen 3cb371 mediumslateblue 7b68ee mediumspringgreen 00fa9a mediumturquoise 48d1cc mediumvioletred c71585 midnightblue 191970 mintcream f5fffa mistyrose ffe4e1 moccasin ffe4b5 navajowhite ffdead navy 000080 "
 "oldlace fdf5e6 olive 808000 olivedrab 6b8e23 orange ffa500 orangered ff4500 orchid da70d6 palegoldenrod eee8aa palegreen 98fb98 paleturquoise afeeee palevioletred db7093 papayawhip ffefd5 peachpuff ffdab9 peru cd853f pink ffc0cb plum dda0dd powderblue b0e0e6 purple 800080 rebeccapurple 663399 red ff0000 rosybrown bc8f8f royalblue 4169e1 "
 "saddlebrown 8b4513 salmon fa8072 sandybrown f4a460 seagreen 2e8b57 seashell fff5ee sienna a0522d silver c0c0c0 skyblue 87ceeb slateblue 6a5acd slategray 708090 slategrey 708090 snow fffafa springgreen 00ff7f steelblue 4682b4 tan d2b48c teal 008080 thistle d8bfd8 tomato ff6347 turquoise 40e0d0 violet ee82ee wheat f5deb3 white ffffff whitesmoke f5f5f5 yellow ffff00 yellowgreen 9acd32";

static int parse_num(const char **pp, int32_t *out)   /* -> x1000 */
{
    const char *p = *pp; int neg = 0; int64_t v = 0; int any = 0, frac = 0, fd = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    while (wu_isdigit(*p) || *p == '.') {
        if (*p == '.') { if (frac) break; frac = 1; p++; continue; }
        any = 1;
        if (!frac) { if (v < 100000000) v = v * 10 + (*p - '0'); }
        else if (fd < 3) { v = v * 10 + (*p - '0'); fd++; }
        p++;
    }
    if (!any) return 0;
    while (frac && fd < 3) { v *= 10; fd++; }
    if (!frac) v *= 1000;
    if ((*p == 'e' || *p == 'E') && (wu_isdigit(p[1]) || ((p[1] == '-' || p[1] == '+') && wu_isdigit(p[2])))) {
        p++; int en = 0, es = 1; if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
        while (wu_isdigit(*p)) { en = en * 10 + (*p - '0'); p++; }
        if (en > 6) en = 6;
        while (en--) { if (es > 0) v *= 10; else v /= 10; }
    }
    if (v > 2000000000LL) v = 2000000000LL;
    *out = neg ? -(int32_t)v : (int32_t)v; *pp = p; return 1;
}

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static uint32_t hsl_to_rgb(int h, int s, int l)   /* h 0..359, s,l 0..1000 */
{
    h %= 360; if (h < 0) h += 360;
    int c = (1000 - (2 * l > 1000 ? 2 * l - 1000 : 1000 - 2 * l)) * s / 1000;
    int hh = h * 1000 / 60; int m2 = hh % 2000; int x = c * (1000 - (m2 > 1000 ? m2 - 1000 : 1000 - m2)) / 1000;
    int m = l - c / 2, r = 0, g = 0, b = 0;
    switch (h / 60) { case 0: r = c; g = x; break; case 1: r = x; g = c; break; case 2: g = c; b = x; break;
                      case 3: g = x; b = c; break; case 4: r = x; b = c; break; default: r = c; b = x; }
    return ((uint32_t)clamp255((r + m) * 255 / 1000) << 16) | ((uint32_t)clamp255((g + m) * 255 / 1000) << 8) | (uint32_t)clamp255((b + m) * 255 / 1000);
}

uint32_t wc_parse_color(const char *s, int *ok)
{
    int dummy; if (!ok) ok = &dummy;
    *ok = 0;
    while (wu_isspace(*s)) s++;
    if (*s == '#') {
        const char *h = s + 1; uint32_t n = 0; while (wu_ishex(h[n])) n++;
        uint32_t v = 0;
        if (n == 3 || n == 4) { for (uint32_t i = 0; i < n; i++) { uint32_t d = (uint32_t)wu_hexval(h[i]); v = (v << 8) | (d << 4) | d; }
            if (n == 3) v = (0xFFu << 24) | v; else v = ((v & 0xFF) << 24) | (v >> 8); *ok = 1; return v; }
        if (n == 6 || n == 8) { for (uint32_t i = 0; i < n; i++) v = (v << 4) | (uint32_t)wu_hexval(h[i]);
            if (n == 6) v |= 0xFF000000u; else v = ((v & 0xFF) << 24) | (v >> 8); *ok = 1; return v; }
        return 0;
    }
    if (wu_startsi(s, "rgb") || wu_startsi(s, "hsl")) {
        int hsl = wu_lower(s[0]) == 'h';
        const char *p = s; while (*p && *p != '(') p++;
        if (*p != '(') return 0;
        p++;
        int32_t v[4] = {0, 0, 0, 1000}; int pct[4] = {0, 0, 0, 0}, k = 0;
        while (*p && *p != ')' && k < 4) {
            while (wu_isspace(*p) || *p == ',' || *p == '/') p++;
            if (*p == ')') break;
            if (!parse_num(&p, &v[k])) return 0;
            if (*p == '%') { pct[k] = 1; p++; }
            else if (hsl && k == 0) { if (wu_startsi(p, "deg")) p += 3; else if (wu_startsi(p, "turn")) { v[0] *= 360; p += 4; } }
            k++;
        }
        if (k < 3) return 0;
        int a = 255;
        if (k == 4) a = clamp255(pct[3] ? v[3] * 255 / 100000 : v[3] * 255 / 1000);
        uint32_t rgb;
        if (hsl) rgb = hsl_to_rgb(v[0] / 1000, v[1] / 100, v[2] / 100);
        else {
            int c[3]; for (int i = 0; i < 3; i++) c[i] = clamp255(pct[i] ? v[i] * 255 / 100000 : (v[i] + 500) / 1000);
            rgb = ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
        }
        *ok = 1; return ((uint32_t)a << 24) | rgb;
    }
    if (wu_startsi(s, "transparent")) { *ok = 1; return 0; }
    {
        char nm[24]; uint32_t n = 0; while (wu_isalpha(s[n]) && n < 23) { nm[n] = (char)wu_lower(s[n]); n++; } nm[n] = 0;
        if (!n || wu_isalnum(s[n])) return 0;
        const char *p = color_names;
        while (*p) {
            const char *q = p; while (*q != ' ') q++;
            uint32_t l = (uint32_t)(q - p);
            const char *hx = q + 1; const char *e = hx; while (*e && *e != ' ') e++;
            if (l == n && wu_strneq(p, nm, n)) { uint32_t v = 0; for (int i = 0; i < 6; i++) v = (v << 4) | (uint32_t)wu_hexval(hx[i]); *ok = 1; return 0xFF000000u | v; }
            p = *e ? e + 1 : e;
        }
    }
    return 0;
}

/* ============================ tokens y longitudes ============================ */
struct lctx { int font_px, root_px, vw, vh; };

/* extrae un token (palabra con parentesis anidados, cadena, o separador ,/) */
static const char *next_tok(const char *p, const char *end, const char **ts, uint32_t *tl)
{
    while (p < end && wu_isspace(*p)) p++;
    if (p >= end) return 0;
    *ts = p;
    if (*p == ',' || *p == '/') { p++; *tl = 1; return p; }
    if (*p == '"' || *p == '\'') { char q = *p++; while (p < end && *p != q) { if (*p == '\\' && p + 1 < end) p++; p++; } if (p < end) p++; *tl = (uint32_t)(p - *ts); return p; }
    int depth = 0;
    while (p < end) {
        if (*p == '(') depth++;
        else if (*p == ')') { if (depth > 0) depth--; }
        else if (depth == 0 && (wu_isspace(*p) || *p == ',' || *p == '/')) { if (*p == '/' && p > *ts && p[-1] != ')' && 0) {} break; }
        else if ((*p == '"' || *p == '\'') && depth > 0) { char q = *p++; while (p < end && *p != q) p++; }
        p++;
    }
    *tl = (uint32_t)(p - *ts); return p;
}

struct lin { int64_t px, pct; int scalar; int64_t sc; };   /* px/pct en milesimas */

static int unit_len(const char *p, uint32_t n)
{
    uint32_t i = 0; while (i < n && (wu_isalpha(p[i]) || p[i] == '%')) i++; return (int)i;
}

static int calc_val(const char **pp, const char *end, const struct lctx *cx, struct lin *o, int depth);

static int calc_factor(const char **pp, const char *end, const struct lctx *cx, struct lin *o, int depth)
{
    const char *p = *pp; while (p < end && wu_isspace(*p)) p++;
    if (p >= end || depth > 6) return 0;
    if (*p == '(') { p++; if (!calc_val(&p, end, cx, o, depth + 1)) return 0; while (p < end && wu_isspace(*p)) p++; if (p < end && *p == ')') p++; *pp = p; return 1; }
    if (wu_startsi(p, "calc(")) { p += 5; if (!calc_val(&p, end, cx, o, depth + 1)) return 0; while (p < end && wu_isspace(*p)) p++; if (p < end && *p == ')') p++; *pp = p; return 1; }
    if (wu_startsi(p, "min(") || wu_startsi(p, "max(") || wu_startsi(p, "clamp(")) {
        int isclamp = wu_lower(p[1]) == 'l', ismin = wu_lower(p[1]) == 'i'; p += isclamp ? 6 : 4;
        struct lin a[3]; int n = 0;
        while (n < 3) { if (!calc_val(&p, end, cx, &a[n], depth + 1)) return 0; n++; while (p < end && wu_isspace(*p)) p++; if (p < end && *p == ',') { p++; continue; } break; }
        if (p < end && *p == ')') p++;
        /* sin contenedor conocido: se elige por el valor en px (aprox. 800 px para %) */
        int64_t k[3]; for (int i = 0; i < n; i++) k[i] = a[i].px + a[i].pct * 8;
        int pick = 0;
        if (isclamp && n == 3) { pick = k[1] < k[0] ? 0 : (k[1] > k[2] ? 2 : 1); }
        else for (int i = 1; i < n; i++) if (ismin ? k[i] < k[pick] : k[i] > k[pick]) pick = i;
        *o = a[pick]; *pp = p; return 1;
    }
    int32_t v;
    if (!parse_num(&p, &v)) return 0;
    uint32_t ul = (uint32_t)unit_len(p, (uint32_t)(end - p));
    o->px = 0; o->pct = 0; o->scalar = 0; o->sc = v;
    if (ul == 0) { o->scalar = 1; }
    else if (ul == 1 && *p == '%') o->pct = v;
    else if (ul == 2 && wu_strnieq(p, "px", 2)) o->px = v;
    else if (ul == 2 && wu_strnieq(p, "em", 2)) o->px = (int64_t)v * cx->font_px;
    else if (ul == 3 && wu_strnieq(p, "rem", 3)) o->px = (int64_t)v * cx->root_px;
    else if (ul == 2 && wu_strnieq(p, "pt", 2)) o->px = (int64_t)v * 4 / 3;
    else if (ul == 2 && wu_strnieq(p, "vw", 2)) o->px = (int64_t)v * cx->vw / 100;
    else if (ul == 2 && wu_strnieq(p, "vh", 2)) o->px = (int64_t)v * cx->vh / 100;
    else if (ul == 4 && wu_strnieq(p, "vmin", 4)) o->px = (int64_t)v * (cx->vw < cx->vh ? cx->vw : cx->vh) / 100;
    else if (ul == 4 && wu_strnieq(p, "vmax", 4)) o->px = (int64_t)v * (cx->vw > cx->vh ? cx->vw : cx->vh) / 100;
    else if (ul == 2 && wu_strnieq(p, "ch", 2)) o->px = (int64_t)v * cx->font_px / 2;
    else if (ul == 2 && wu_strnieq(p, "ex", 2)) o->px = (int64_t)v * cx->font_px / 2;
    else if (ul == 2 && wu_strnieq(p, "cm", 2)) o->px = (int64_t)v * 3780 / 100;
    else if (ul == 2 && wu_strnieq(p, "mm", 2)) o->px = (int64_t)v * 378 / 100;
    else if (ul == 2 && wu_strnieq(p, "in", 2)) o->px = (int64_t)v * 96;
    else if (ul == 2 && wu_strnieq(p, "pc", 2)) o->px = (int64_t)v * 16;
    else if (ul == 1 && wu_lower(*p) == 'q') o->px = (int64_t)v * 945 / 1000;
    else if (ul == 2 && wu_strnieq(p, "fr", 2)) { o->scalar = 1; *pp = p; return 1; }
    else return 0;
    p += ul; *pp = p; return 1;
}

static int calc_term(const char **pp, const char *end, const struct lctx *cx, struct lin *o, int depth)
{
    if (!calc_factor(pp, end, cx, o, depth)) return 0;
    for (;;) {
        const char *p = *pp; while (p < end && wu_isspace(*p)) p++;
        if (p >= end || (*p != '*' && *p != '/')) return 1;
        char op = *p++; *pp = p;
        struct lin b; if (!calc_factor(pp, end, cx, &b, depth)) return 0;
        if (op == '*') {
            if (b.scalar) { o->px = o->px * b.sc / 1000; o->pct = o->pct * b.sc / 1000; if (o->scalar) o->sc = o->sc * b.sc / 1000; }
            else if (o->scalar) { int64_t s = o->sc; *o = b; o->px = o->px * s / 1000; o->pct = o->pct * s / 1000; }
            else return 0;
        } else {
            if (!b.scalar || b.sc == 0) return 0;
            o->px = o->px * 1000 / b.sc; o->pct = o->pct * 1000 / b.sc; if (o->scalar) o->sc = o->sc * 1000 / b.sc;
        }
    }
}

static int calc_val(const char **pp, const char *end, const struct lctx *cx, struct lin *o, int depth)
{
    if (!calc_term(pp, end, cx, o, depth)) return 0;
    for (;;) {
        const char *p = *pp; while (p < end && wu_isspace(*p)) p++;
        if (p >= end || (*p != '+' && *p != '-')) return 1;
        char op = *p++; *pp = p;
        struct lin b; if (!calc_term(pp, end, cx, &b, depth)) return 0;
        if (op == '+') { o->px += b.px; o->pct += b.pct; } else { o->px -= b.px; o->pct -= b.pct; }
        o->scalar = o->scalar && b.scalar; if (o->scalar) o->sc += (op == '+' ? b.sc : -b.sc);
    }
}

static int parse_len(const char *t, uint32_t n, const struct lctx *cx, struct wlen *o, int allow_num_as_px)
{
    char b[96]; if (n >= sizeof(b)) return 0;
    memcpy(b, t, n); b[n] = 0;
    memset(o, 0, sizeof(*o));
    if (wu_strieq(b, "auto")) { o->kind = LK_AUTO; return 1; }
    if (wu_strieq(b, "none") || wu_strieq(b, "normal")) { o->kind = LK_NONE; return 1; }
    if (wu_strieq(b, "max-content") || wu_strieq(b, "fit-content") || wu_strieq(b, "min-content") || wu_strieq(b, "-webkit-fill-available") || wu_strieq(b, "stretch")) { o->kind = LK_AUTO; return 1; }
    {
        const char *p = b; struct lin l;
        if (!calc_val(&p, b + n, cx, &l, 0)) return 0;
        while (wu_isspace(*p)) p++;
        if (*p && *p != ')') {
            /* fr */
            if (l.scalar == 0 && l.px == 0 && l.pct == 0) return 0;
        }
        if (*p && wu_strnieq(p, "fr", 2)) { o->kind = LK_FR; o->px = (int32_t)(l.sc / 10); return 1; }
        if (*p && *p != ')') return 0;
        if (l.scalar && !allow_num_as_px) { if (l.sc != 0) return 0; }
        o->kind = LK_VAL;
        o->px = (int32_t)((l.scalar ? l.sc : l.px + (l.px >= 0 ? 500 : -500)) / 1000);
        if (l.scalar) o->px = (int32_t)(l.sc / 1000);
        o->pct = (int32_t)(l.pct / 10);
    }
    return 1;
}

int wc_parse_length_px(const char *s, int base)
{
    struct lctx cx = { 16, 16, 1024, 768 }; struct wlen l;
    if (!parse_len(s, wu_strlen(s), &cx, &l, 1) || l.kind != LK_VAL) return 0;
    return l.px + (int)((int64_t)base * l.pct / 10000);
}

int wc_line_px(const struct wstyle *s)
{
    if (s->lh_mode == 2) return s->lh_val;
    if (s->lh_mode == 1) return (s->font_px * s->lh_val + 50) / 100;
    return (s->font_px * 118 + 50) / 100;
}

/* ============================ propiedades ============================ */
#define PROPLIST(X) \
 X(DISPLAY,"display") X(POSITION,"position") X(FLOAT,"float") X(CLEAR,"clear") X(VISIBILITY,"visibility") X(OVERFLOW,"overflow") \
 X(OVERFLOW_X,"overflow-x") X(OVERFLOW_Y,"overflow-y") X(Z_INDEX,"z-index") X(TOP,"top") X(RIGHT,"right") X(BOTTOM,"bottom") X(LEFT,"left") \
 X(INSET,"inset") X(WIDTH,"width") X(HEIGHT,"height") X(MIN_WIDTH,"min-width") X(MAX_WIDTH,"max-width") X(MIN_HEIGHT,"min-height") \
 X(MAX_HEIGHT,"max-height") X(MARGIN,"margin") X(MARGIN_TOP,"margin-top") X(MARGIN_RIGHT,"margin-right") X(MARGIN_BOTTOM,"margin-bottom") \
 X(MARGIN_LEFT,"margin-left") X(PADDING,"padding") X(PADDING_TOP,"padding-top") X(PADDING_RIGHT,"padding-right") X(PADDING_BOTTOM,"padding-bottom") \
 X(PADDING_LEFT,"padding-left") X(BOX_SIZING,"box-sizing") X(BORDER,"border") X(BORDER_TOP,"border-top") X(BORDER_RIGHT,"border-right") \
 X(BORDER_BOTTOM,"border-bottom") X(BORDER_LEFT,"border-left") X(BORDER_WIDTH,"border-width") X(BORDER_STYLE,"border-style") X(BORDER_COLOR,"border-color") \
 X(BORDER_TOP_WIDTH,"border-top-width") X(BORDER_RIGHT_WIDTH,"border-right-width") X(BORDER_BOTTOM_WIDTH,"border-bottom-width") X(BORDER_LEFT_WIDTH,"border-left-width") \
 X(BORDER_TOP_STYLE,"border-top-style") X(BORDER_RIGHT_STYLE,"border-right-style") X(BORDER_BOTTOM_STYLE,"border-bottom-style") X(BORDER_LEFT_STYLE,"border-left-style") \
 X(BORDER_TOP_COLOR,"border-top-color") X(BORDER_RIGHT_COLOR,"border-right-color") X(BORDER_BOTTOM_COLOR,"border-bottom-color") X(BORDER_LEFT_COLOR,"border-left-color") \
 X(BORDER_RADIUS,"border-radius") X(OPACITY,"opacity") X(FLEX,"flex") X(FLEX_GROW,"flex-grow") X(FLEX_SHRINK,"flex-shrink") X(FLEX_BASIS,"flex-basis") \
 X(FLEX_DIRECTION,"flex-direction") X(FLEX_WRAP,"flex-wrap") X(FLEX_FLOW,"flex-flow") X(JUSTIFY_CONTENT,"justify-content") X(ALIGN_ITEMS,"align-items") \
 X(ALIGN_SELF,"align-self") X(ALIGN_CONTENT,"align-content") X(ORDER,"order") X(GAP,"gap") X(GRID_GAP,"grid-gap") X(ROW_GAP,"row-gap") X(COLUMN_GAP,"column-gap") \
 X(GRID_TEMPLATE_COLUMNS,"grid-template-columns") X(GRID_COLUMN,"grid-column") X(COLOR,"color") X(FONT_SIZE,"font-size") X(FONT_WEIGHT,"font-weight") \
 X(FONT_STYLE,"font-style") X(FONT_FAMILY,"font-family") X(FONT,"font") X(LINE_HEIGHT,"line-height") X(TEXT_ALIGN,"text-align") X(TEXT_DECORATION,"text-decoration") \
 X(TEXT_DECORATION_LINE,"text-decoration-line") X(TEXT_TRANSFORM,"text-transform") X(TEXT_INDENT,"text-indent") X(LETTER_SPACING,"letter-spacing") \
 X(WHITE_SPACE,"white-space") X(VERTICAL_ALIGN,"vertical-align") X(TEXT_OVERFLOW,"text-overflow") X(WORD_BREAK,"word-break") X(OVERFLOW_WRAP,"overflow-wrap") \
 X(WORD_WRAP,"word-wrap") X(LIST_STYLE,"list-style") X(LIST_STYLE_TYPE,"list-style-type") X(LIST_STYLE_POSITION,"list-style-position") \
 X(BACKGROUND,"background") X(BACKGROUND_COLOR,"background-color") X(BACKGROUND_IMAGE,"background-image") X(BACKGROUND_REPEAT,"background-repeat") \
 X(BACKGROUND_SIZE,"background-size") X(BACKGROUND_POSITION,"background-position") X(BORDER_COLLAPSE,"border-collapse") X(BORDER_SPACING,"border-spacing") \
 X(CURSOR,"cursor") X(TOP_LEFT_RADIUS,"border-top-left-radius") X(TOP_RIGHT_RADIUS,"border-top-right-radius") X(BOTTOM_LEFT_RADIUS,"border-bottom-left-radius") \
 X(BOTTOM_RIGHT_RADIUS,"border-bottom-right-radius") X(PLACE_ITEMS,"place-items") X(INSET_BLOCK_START,"inset-block-start") X(MARGIN_INLINE,"margin-inline") \
 X(MARGIN_BLOCK,"margin-block") X(PADDING_INLINE,"padding-inline") X(PADDING_BLOCK,"padding-block") X(BACKGROUND_POSITION_X,"background-position-x") \
 X(BOX_SHADOW,"box-shadow")
static const char *findn(const char *h, uint32_t hl, const char *n);   /* SOMA_PARCHE_028_UI (definida mas abajo) */
enum { PR_NONE = 0,
#define X(a, b) PR_##a,
PROPLIST(X)
#undef X
PR__COUNT, PR_CUSTOM };
static const char *const prop_names[] = { "",
#define X(a, b) b,
PROPLIST(X)
#undef X
};

static int prop_id(const char *n, uint32_t l)
{
    if (l >= 2 && n[0] == '-' && n[1] == '-') return PR_CUSTOM;
    if (l > 8 && n[0] == '-' && n[1] == 'w' && wu_strneq(n, "-webkit-", 8)) { n += 8; l -= 8; }
    char b[40]; if (l >= sizeof(b)) return 0;
    for (uint32_t i = 0; i < l; i++) b[i] = (char)wu_lower(n[i]);
    b[l] = 0;
    for (int i = 1; i < PR__COUNT; i++) if (wu_streq(prop_names[i], b)) return i;
    return 0;
}

struct wdecl { uint16_t prop, vlen; uint8_t imp; const char *val; const char *name; uint16_t nlen; };

struct wsel;
struct wrule { struct wsel *sel; struct wdecl *decls; uint16_t nd; uint8_t origin; uint16_t minw, maxw; uint32_t order; struct wrule *bnext; };

struct wcss {
    struct wdoc *d;
    struct wrule **rules; int nrules, cap;
    struct wrule *univ; struct wrule *tagt[WT_COUNT]; struct wrule *idt[128]; struct wrule *clst[128];
    char **imports; int nimp, cimp, imp_next;
    uint32_t order;
    int vw, vh;
    struct wrule **match; int match_cap;
    char *scratch;                 /* expansion de var() */
    struct wlen cur_tmp;
    int pe_seen;
};

/* ---------------- aplicar propiedades ---------------- */
struct actx { struct lctx lc; const struct wstyle *par; struct wcss *c; };

static int is_kw(const char *t, uint32_t n, const char *k) { return wu_strlen(k) == n && wu_strnieq(t, k, n); }
static int tok_is_color(const char *t, uint32_t n) { char b[64]; if (n >= sizeof(b)) return 0; memcpy(b, t, n); b[n] = 0; int ok; wc_parse_color(b, &ok); return ok; }
static uint32_t tok_color(const char *t, uint32_t n, uint32_t cur, int *ok)
{
    char b[64]; *ok = 0; if (n >= sizeof(b)) return 0; memcpy(b, t, n); b[n] = 0;
    if (wu_strieq(b, "currentcolor")) { *ok = 1; return cur; }
    return wc_parse_color(b, ok);
}

static void set_len4(struct wlen *arr, int side_mask, const struct wlen *l) { for (int i = 0; i < 4; i++) if (side_mask & (1 << i)) arr[i] = *l; }

/* analiza hasta 4 longitudes (margin/padding) */
static void apply_box4(struct actx *a, struct wlen *arr, const char *v, uint32_t vl, int neg_ok)
{
    struct wlen l[4]; int n = 0; const char *p = v, *e = v + vl, *ts; uint32_t tl;
    while (n < 4 && (p = next_tok(p, e, &ts, &tl))) {
        if (!parse_len(ts, tl, &a->lc, &l[n], 1)) return;
        if (!neg_ok && l[n].kind == LK_VAL && (l[n].px < 0 || l[n].pct < 0)) return;
        n++;
    }
    if (!n) return;
    struct wlen t = l[0], r = n > 1 ? l[1] : l[0], b = n > 2 ? l[2] : l[0], le = n > 3 ? l[3] : r;
    arr[0] = t; arr[1] = r; arr[2] = b; arr[3] = le;
}

static int border_style_of(const char *t, uint32_t n)
{
    if (is_kw(t, n, "none") || is_kw(t, n, "hidden")) return 0;
    if (is_kw(t, n, "dashed")) return 2;
    if (is_kw(t, n, "dotted")) return 3;
    if (is_kw(t, n, "solid") || is_kw(t, n, "double") || is_kw(t, n, "groove") || is_kw(t, n, "ridge") || is_kw(t, n, "inset") || is_kw(t, n, "outset")) return 1;
    return -1;
}

static void apply_border_one(struct actx *a, struct wstyle *st, int mask, const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int w = 3, sty = 0, ok; uint32_t col = st->color; int seen_style = 0;
    while ((p = next_tok(p, e, &ts, &tl))) {
        int bs = border_style_of(ts, tl);
        if (bs >= 0) { sty = bs; seen_style = 1; continue; }
        if (is_kw(ts, tl, "thin")) { w = 1; continue; } if (is_kw(ts, tl, "medium")) { w = 3; continue; } if (is_kw(ts, tl, "thick")) { w = 5; continue; }
        uint32_t c = tok_color(ts, tl, st->color, &ok);
        if (ok) { col = c; continue; }
        struct wlen l; if (parse_len(ts, tl, &a->lc, &l, 0) && l.kind == LK_VAL) w = l.px;
    }
    if (!seen_style) sty = 0;
    if (w < 0) w = 0;
    if (w > 200) w = 200;
    for (int i = 0; i < 4; i++) if (mask & (1 << i)) { st->border_w[i] = (int16_t)(sty ? w : 0); st->border_style[i] = (uint8_t)sty; st->border_color[i] = col; }
}

static void apply_bg_token(struct actx *a, struct wstyle *st, const char *ts, uint32_t tl, struct wcss *c)
{
    (void)c;
    int ok; uint32_t col = tok_color(ts, tl, st->color, &ok);
    if (ok && !(tl > 3 && wu_strnieq(ts, "url", 3))) { st->bg_color = col; return; }
    if (tl > 4 && wu_strnieq(ts, "url(", 4)) {
        const char *p = ts + 4, *e = ts + tl - 1; while (p < e && wu_isspace(*p)) p++;
        char q = 0; if (p < e && (*p == '"' || *p == '\'')) q = *p++;
        const char *s = p; while (p < e && (q ? *p != q : !wu_isspace(*p))) p++;
        st->bg_image = wa_strndup(&a->c->d->ar, s, (uint32_t)(p - s)); return;
    }
    if (tl > 16 && (wu_strnieq(ts, "linear-gradient(", 16) || wu_strnieq(ts, "repeating-linear-gradient(", 26) || 0)) {
        const char *p = ts + 16, *e = ts + tl - 1; const char *s2; uint32_t l2; int n = 0; uint32_t first = 0, last = 0; int dir = 0, rev = 0;
        if (wu_strnieq(ts, "repeating", 9)) p = ts + 26;
        while ((p = next_tok(p, e, &s2, &l2))) {
            if (l2 == 1 && *s2 == ',') continue;
            if (n == 0 && (is_kw(s2, l2, "to") )) {
                const char *s3; uint32_t l3; p = next_tok(p, e, &s3, &l3); if (p && (is_kw(s3, l3, "right") || is_kw(s3, l3, "left"))) { dir = 1; rev = is_kw(s3, l3, "left"); } else if (p && is_kw(s3, l3, "top")) rev = 1;
                const char *s4; uint32_t l4; const char *p2 = next_tok(p, e, &s4, &l4); (void)p2; (void)s4; (void)l4;
                continue;
            }
            if (l2 && (wu_isdigit(*s2) || *s2 == '-') && l2 > 3 && wu_strnieq(s2 + l2 - 3, "deg", 3)) { int d = wu_atoi(s2); d = ((d % 360) + 360) % 360; if (d == 90 || d == 270) dir = 1; if (d == 270 || d == 0) rev = 1; continue; }
            int ok2; uint32_t cc = tok_color(s2, l2, st->color, &ok2);
            if (ok2) { if (n == 0) first = cc; last = cc; n++; }
        }
        if (n >= 1) { st->has_grad = 1; st->grad_dir = (uint8_t)dir; st->grad_a = rev ? last : first; st->grad_b = rev ? first : last; if (n == 1) st->grad_b = st->grad_a; }
        return;
    }
    if ((tl > 16 && wu_strnieq(ts, "radial-gradient(", 16)) || (tl > 15 && wu_strnieq(ts, "conic-gradient(", 15)) || (tl > 26 && wu_strnieq(ts, "repeating-radial-gradient(", 26))) {
        /* SOMA_PARCHE_028_UI: los degradados radiales y conicos se aproximan con uno lineal vertical entre su primer y su ultimo color */
        const char *p = ts; while (p < ts + tl && *p != '(') p++; p++;
        const char *e = ts + tl - 1, *s2; uint32_t l2; int n = 0; uint32_t first = 0, last = 0;
        while ((p = next_tok(p, e, &s2, &l2))) { int ok2; uint32_t cc = tok_color(s2, l2, st->color, &ok2); if (ok2) { if (n == 0) first = cc; last = cc; n++; } }
        if (n >= 1) { st->has_grad = 1; st->grad_dir = 0; st->grad_a = first; st->grad_b = n > 1 ? last : first; }
        return;
    }
    if (is_kw(ts, tl, "no-repeat")) st->bg_repeat = 3; else if (is_kw(ts, tl, "repeat-x")) st->bg_repeat = 1; else if (is_kw(ts, tl, "repeat-y")) st->bg_repeat = 2; else if (is_kw(ts, tl, "repeat")) st->bg_repeat = 0;
    else if (is_kw(ts, tl, "none")) { st->bg_image = 0; st->has_grad = 0; }
    else if (is_kw(ts, tl, "cover")) st->bg_size_mode = 1; else if (is_kw(ts, tl, "contain")) st->bg_size_mode = 2;
    else if (is_kw(ts, tl, "center")) { st->bg_pos_x = 50; st->bg_pos_y = 50; }
    else if (is_kw(ts, tl, "left")) st->bg_pos_x = 0; else if (is_kw(ts, tl, "right")) st->bg_pos_x = 100;
    else if (is_kw(ts, tl, "top")) st->bg_pos_y = 0; else if (is_kw(ts, tl, "bottom")) st->bg_pos_y = 100;
}

static int family_is_mono(const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl;
    while (p < e) {
        while (p < e && (wu_isspace(*p) || *p == ',' || *p == '"' || *p == '\'')) p++;
        const char *s = p; while (p < e && *p != ',') p++;
        uint32_t l = (uint32_t)(p - s); char b[48]; if (l >= sizeof(b)) l = sizeof(b) - 1;
        for (uint32_t i = 0; i < l; i++) b[i] = (char)wu_lower(s[i]);
        b[l] = 0;
        if (wu_find(b, "mono") || wu_find(b, "courier") || wu_find(b, "consolas") || wu_find(b, "menlo") || wu_find(b, "typewriter") || wu_find(b, "terminal")) return 1;
        if (wu_find(b, "sans") || wu_find(b, "serif") || wu_find(b, "arial") || wu_find(b, "helvetica") || wu_find(b, "verdana") || wu_find(b, "segoe") ||
            wu_find(b, "roboto") || wu_find(b, "system-ui") || wu_find(b, "georgia") || wu_find(b, "times") || wu_find(b, "tahoma") || wu_find(b, "inter") || wu_find(b, "cursive")) return 0;
    }
    return 0;
}

static int font_size_of(struct actx *a, const char *t, uint32_t n, int *out)
{
    static const struct { const char *k; int px; } kw[] = { {"xx-small", 9}, {"x-small", 10}, {"small", 13}, {"medium", 16}, {"large", 18}, {"x-large", 24}, {"xx-large", 32}, {"xxx-large", 48} };
    for (unsigned i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) if (is_kw(t, n, kw[i].k)) { *out = kw[i].px; return 1; }
    if (is_kw(t, n, "smaller")) { *out = a->lc.font_px * 83 / 100; return 1; }
    if (is_kw(t, n, "larger")) { *out = a->lc.font_px * 120 / 100; return 1; }
    struct wlen l; if (!parse_len(t, n, &a->lc, &l, 0) || l.kind != LK_VAL) return 0;
    int px = l.px + (int)((int64_t)a->lc.font_px * l.pct / 10000);
    if (px < 0) return 0;
    *out = px; return 1;
}

static void apply_lh(struct actx *a, struct wstyle *st, const char *t, uint32_t n)
{
    if (is_kw(t, n, "normal")) { st->lh_mode = 0; return; }
    char b[32]; if (n >= sizeof(b)) return; memcpy(b, t, n); b[n] = 0;
    const char *p = b; int32_t v;
    if (parse_num(&p, &v) && *p == 0) { st->lh_mode = 1; st->lh_val = (int16_t)(v / 10); return; }
    struct wlen l; if (!parse_len(t, n, &a->lc, &l, 0) || l.kind != LK_VAL) return;
    st->lh_mode = 2; st->lh_val = (int16_t)(l.px + (int)((int64_t)st->font_px * l.pct / 10000));
}

static void apply_font_shorthand(struct actx *a, struct wstyle *st, const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int got_size = 0;
    st->italic = 0; st->bold = 0;
    while ((p = next_tok(p, e, &ts, &tl))) {
        if (is_kw(ts, tl, "italic") || is_kw(ts, tl, "oblique")) { st->italic = 1; continue; }
        if (is_kw(ts, tl, "bold") || is_kw(ts, tl, "bolder")) { st->bold = 1; continue; }
        if (is_kw(ts, tl, "normal") || is_kw(ts, tl, "small-caps")) continue;
        if (tl == 3 && wu_isdigit(ts[0]) && ts[1] == '0' && ts[2] == '0') { st->bold = ts[0] >= '6'; continue; }
        if (!got_size) {
            const char *sl = ts; while (sl < ts + tl && *sl != '/') sl++;
            int px; if (font_size_of(a, ts, (uint32_t)(sl - ts), &px)) {
                st->font_px = (int16_t)(px < 1 ? 1 : (px > 400 ? 400 : px)); a->lc.font_px = st->font_px; got_size = 1;
                if (sl < ts + tl) apply_lh(a, st, sl + 1, (uint32_t)(ts + tl - sl - 1));
                continue;
            }
        } else {
            if (tl == 1 && *ts == '/') { const char *q = next_tok(p, e, &ts, &tl); if (q) { apply_lh(a, st, ts, tl); p = q; } continue; }
            st->mono = (uint8_t)family_is_mono(ts, (uint32_t)(e - ts));
            break;
        }
    }
}

static void apply_flex(struct actx *a, struct wstyle *st, const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int idx = 0;
    if (is_kw(v, vl, "none")) { st->flex_grow = 0; st->flex_shrink = 0; st->flex_basis.kind = LK_AUTO; return; }
    if (is_kw(v, vl, "auto")) { st->flex_grow = 100; st->flex_shrink = 100; st->flex_basis.kind = LK_AUTO; return; }
    st->flex_grow = 0; st->flex_shrink = 100; st->flex_basis.kind = LK_VAL; st->flex_basis.px = 0; st->flex_basis.pct = 0;
    while ((p = next_tok(p, e, &ts, &tl))) {
        char b[24]; if (tl >= sizeof(b)) continue; memcpy(b, ts, tl); b[tl] = 0;
        const char *q = b; int32_t nv;
        if (parse_num(&q, &nv) && *q == 0 && idx < 2) { if (idx == 0) st->flex_grow = (int16_t)(nv / 10); else st->flex_shrink = (int16_t)(nv / 10); idx++; if (idx == 1 && 0) {} continue; }
        struct wlen l; if (parse_len(ts, tl, &a->lc, &l, 0)) st->flex_basis = l;
    }
}

static int align_of(const char *t, uint32_t n)
{
    if (is_kw(t, n, "stretch") || is_kw(t, n, "normal")) return AI_STRETCH;
    if (is_kw(t, n, "flex-start") || is_kw(t, n, "start") || is_kw(t, n, "self-start") || is_kw(t, n, "left")) return AI_START;
    if (is_kw(t, n, "flex-end") || is_kw(t, n, "end") || is_kw(t, n, "self-end") || is_kw(t, n, "right")) return AI_END;
    if (is_kw(t, n, "center")) return AI_CENTER;
    if (is_kw(t, n, "baseline")) return AI_BASELINE;
    if (is_kw(t, n, "auto")) return AI_AUTO;
    if (is_kw(t, n, "space-between")) return 10 + JC_BETWEEN;
    if (is_kw(t, n, "space-around")) return 10 + JC_AROUND;
    if (is_kw(t, n, "space-evenly")) return 10 + JC_EVENLY;
    return -1;
}

static void apply_grid_cols(struct actx *a, struct wstyle *st, const char *v, uint32_t vl)
{
    const char *p = v, *e = v + vl, *ts; uint32_t tl; int n = 0;
    memset(st->grid_tracks, 0, sizeof(st->grid_tracks)); st->grid_cols = 0; st->grid_auto_min = 0;
    while (n < 8 && (p = next_tok(p, e, &ts, &tl))) {
        if (tl > 7 && wu_strnieq(ts, "repeat(", 7)) {
            const char *q = ts + 7, *qe = ts + tl - 1, *s2; uint32_t l2; int cnt = 0; int autoc = 0;
            q = next_tok(q, qe, &s2, &l2); if (!q) continue;
            if (is_kw(s2, l2, "auto-fill") || is_kw(s2, l2, "auto-fit")) autoc = 1; else cnt = wu_atoi(s2);
            q = next_tok(q, qe, &s2, &l2); /* coma */
            struct wlen tr[4]; int nt = 0;
            while (nt < 4 && (q = next_tok(q, qe, &s2, &l2))) {
                if (l2 > 7 && wu_strnieq(s2, "minmax(", 7)) {
                    const char *m = s2 + 7, *me = s2 + l2 - 1, *s3; uint32_t l3; m = next_tok(m, me, &s3, &l3); m = next_tok(m, me, &s3, &l3); m = next_tok(m, me, &s3, &l3);
                    if (m || l3) { if (!parse_len(s3, l3, &a->lc, &tr[nt], 0)) { tr[nt].kind = LK_FR; tr[nt].px = 100; } nt++; }
                } else if (parse_len(s2, l2, &a->lc, &tr[nt], 0)) nt++;
            }
            if (autoc) { int w = tr[0].kind == LK_VAL && tr[0].px > 0 ? tr[0].px : 200; st->grid_auto_min = (int16_t)(w > 4000 ? 4000 : w); cnt = a->c->vw / (w + 16); if (cnt < 1) cnt = 1; if (cnt > 8) cnt = 8; if (tr[0].kind == LK_VAL) { tr[0].kind = LK_FR; tr[0].px = 100; } }
            for (int c2 = 0; c2 < cnt && n < 8; c2++) for (int k = 0; k < nt && n < 8; k++) st->grid_tracks[n++] = tr[k];
            continue;
        }
        if (tl > 7 && wu_strnieq(ts, "minmax(", 7)) {
            const char *m = ts + 7, *me = ts + tl - 1, *s3; uint32_t l3; m = next_tok(m, me, &s3, &l3); m = next_tok(m, me, &s3, &l3); m = next_tok(m, me, &s3, &l3);
            if (!parse_len(s3, l3, &a->lc, &st->grid_tracks[n], 0)) { st->grid_tracks[n].kind = LK_FR; st->grid_tracks[n].px = 100; }
            n++; continue;
        }
        if (is_kw(ts, tl, "none")) continue;
        if (ts[0] == '[') continue;
        if (parse_len(ts, tl, &a->lc, &st->grid_tracks[n], 0)) n++;
    }
    st->grid_cols = (int16_t)n;
}

static void apply_prop(struct actx *a, struct wstyle *st, int prop, const char *v, uint32_t vl, const char *cname, uint32_t cnl)
{
    while (vl && wu_isspace(v[0])) { v++; vl--; }
    while (vl && wu_isspace(v[vl - 1])) vl--;
    if (!vl && prop != PR_CUSTOM) return;
    const struct wstyle *par = a->par;
    struct wlen l; const char *ts; uint32_t tl; int ok;
    int inherit = is_kw(v, vl, "inherit");
    if (is_kw(v, vl, "initial") || is_kw(v, vl, "unset") || inherit) {
        /* soporte limitado: inherit/unset de las propiedades mas comunes */
        if (inherit || is_kw(v, vl, "unset")) {
            switch (prop) {
            case PR_COLOR: st->color = par->color; return;
            case PR_FONT_SIZE: st->font_px = par->font_px; return;
            case PR_FONT_WEIGHT: st->bold = par->bold; return;
            case PR_TEXT_ALIGN: st->text_align = par->text_align; return;
            case PR_BACKGROUND_COLOR: st->bg_color = par->bg_color; return;
            case PR_VISIBILITY: st->visibility = par->visibility; return;
            case PR_LINE_HEIGHT: st->lh_mode = par->lh_mode; st->lh_val = par->lh_val; return;
            case PR_DISPLAY: st->display = par->display; return;
            case PR_WIDTH: st->width = par->width; return;
            case PR_HEIGHT: st->height = par->height; return;
            case PR_BORDER_COLOR: for (int i = 0; i < 4; i++) st->border_color[i] = par->border_color[i]; return;
            default: break;
            }
        }
        if (prop == PR_BACKGROUND_COLOR || prop == PR_BACKGROUND) { st->bg_color = 0; if (prop == PR_BACKGROUND) { st->bg_image = 0; st->has_grad = 0; } return; }
        if (prop == PR_WIDTH) { st->width.kind = LK_AUTO; return; } if (prop == PR_HEIGHT) { st->height.kind = LK_AUTO; return; }
        if (prop == PR_DISPLAY) { st->display = D_INLINE; return; }
        if (prop == PR_COLOR) { st->color = 0xFF000000u; return; }
        if (prop == PR_OPACITY) { st->opacity = 255; return; }
        if (prop == PR_MARGIN) { for (int i = 0; i < 4; i++) { st->margin[i].kind = LK_VAL; st->margin[i].px = st->margin[i].pct = 0; } return; }
        if (prop == PR_PADDING) { for (int i = 0; i < 4; i++) { st->padding[i].kind = LK_VAL; st->padding[i].px = st->padding[i].pct = 0; } return; }
        if (prop == PR_BORDER) { for (int i = 0; i < 4; i++) { st->border_w[i] = 0; st->border_style[i] = 0; } return; }
        return;
    }
    switch (prop) {
    case PR_DISPLAY: {
        static const struct { const char *k; uint8_t d; } dk[] = { {"none", D_NONE}, {"block", D_BLOCK}, {"inline", D_INLINE}, {"inline-block", D_INLINE_BLOCK}, {"list-item", D_LIST_ITEM},
            {"flex", D_FLEX}, {"inline-flex", D_INLINE_FLEX}, {"-webkit-box", D_FLEX}, {"-webkit-inline-box", D_INLINE_FLEX}, {"grid", D_GRID}, {"inline-grid", D_GRID}, {"table", D_TABLE}, {"inline-table", D_INLINE_TABLE},
            {"table-row", D_TABLE_ROW}, {"table-cell", D_TABLE_CELL}, {"table-row-group", D_TABLE_GROUP}, {"table-header-group", D_TABLE_GROUP}, {"table-footer-group", D_TABLE_GROUP},
            {"table-caption", D_TABLE_CAPTION}, {"table-column", D_NONE}, {"table-column-group", D_NONE}, {"contents", D_CONTENTS}, {"flow-root", D_BLOCK}, {"ruby", D_INLINE} };
        for (unsigned i = 0; i < sizeof(dk) / sizeof(dk[0]); i++) if (is_kw(v, vl, dk[i].k)) { st->display = dk[i].d; return; }
        return; }
    case PR_POSITION: st->position = is_kw(v, vl, "relative") ? P_RELATIVE : is_kw(v, vl, "absolute") ? P_ABSOLUTE : is_kw(v, vl, "fixed") ? P_FIXED : is_kw(v, vl, "sticky") ? P_STICKY : P_STATIC; return;
    case PR_FLOAT: st->floating = is_kw(v, vl, "left") || is_kw(v, vl, "inline-start") ? FL_LEFT : is_kw(v, vl, "right") || is_kw(v, vl, "inline-end") ? FL_RIGHT : FL_NONE; return;
    case PR_CLEAR: st->clear = is_kw(v, vl, "left") ? 1 : is_kw(v, vl, "right") ? 2 : is_kw(v, vl, "both") ? 3 : 0; return;
    case PR_VISIBILITY: st->visibility = !is_kw(v, vl, "visible"); return;
    case PR_OVERFLOW: case PR_OVERFLOW_X: case PR_OVERFLOW_Y: {
        const char *p = next_tok(v, v + vl, &ts, &tl); if (!p) return;
        uint8_t o = (is_kw(ts, tl, "hidden") || is_kw(ts, tl, "clip")) ? 1 : (is_kw(ts, tl, "scroll") || is_kw(ts, tl, "auto")) ? 2 : 0;
        if (prop != PR_OVERFLOW_Y) st->overflow_x = o;
        if (prop != PR_OVERFLOW_X) st->overflow_y = o;
        if (prop == PR_OVERFLOW && next_tok(p, v + vl, &ts, &tl)) st->overflow_y = (is_kw(ts, tl, "hidden") || is_kw(ts, tl, "clip")) ? 1 : (is_kw(ts, tl, "scroll") || is_kw(ts, tl, "auto")) ? 2 : 0;
        return; }
    case PR_Z_INDEX: if (is_kw(v, vl, "auto")) st->z_index = 0; else st->z_index = (int16_t)wu_atoi(v); return;
    case PR_TOP: if (parse_len(v, vl, &a->lc, &l, 1)) st->top = l; return;
    case PR_RIGHT: if (parse_len(v, vl, &a->lc, &l, 1)) st->right = l; return;
    case PR_BOTTOM: if (parse_len(v, vl, &a->lc, &l, 1)) st->bottom = l; return;
    case PR_LEFT: if (parse_len(v, vl, &a->lc, &l, 1)) st->left = l; return;
    case PR_INSET: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 1); st->top = b4[0]; st->right = b4[1]; st->bottom = b4[2]; st->left = b4[3]; return; }
    case PR_INSET_BLOCK_START: if (parse_len(v, vl, &a->lc, &l, 1)) st->top = l; return;
    case PR_WIDTH: if (parse_len(v, vl, &a->lc, &l, 0) && !(l.kind == LK_VAL && l.px < 0 && l.pct <= 0)) st->width = l; return;
    case PR_HEIGHT: if (parse_len(v, vl, &a->lc, &l, 0) && !(l.kind == LK_VAL && l.px < 0 && l.pct <= 0)) st->height = l; return;
    case PR_MIN_WIDTH: if (parse_len(v, vl, &a->lc, &l, 0)) { if (l.kind != LK_VAL) { l.kind = LK_VAL; l.px = 0; l.pct = 0; } st->min_w = l; } return;
    case PR_MIN_HEIGHT: if (parse_len(v, vl, &a->lc, &l, 0)) { if (l.kind != LK_VAL) { l.kind = LK_VAL; l.px = 0; l.pct = 0; } st->min_h = l; } return;
    case PR_MAX_WIDTH: if (parse_len(v, vl, &a->lc, &l, 0)) { if (l.kind == LK_AUTO) l.kind = LK_NONE; st->max_w = l; } return;
    case PR_MAX_HEIGHT: if (parse_len(v, vl, &a->lc, &l, 0)) { if (l.kind == LK_AUTO) l.kind = LK_NONE; st->max_h = l; } return;
    case PR_MARGIN: apply_box4(a, st->margin, v, vl, 1); return;
    case PR_MARGIN_INLINE: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 1); st->margin[1] = b4[1]; st->margin[3] = b4[3]; return; }
    case PR_MARGIN_BLOCK: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 1); st->margin[0] = b4[0]; st->margin[2] = b4[2]; return; }
    case PR_MARGIN_TOP: case PR_MARGIN_RIGHT: case PR_MARGIN_BOTTOM: case PR_MARGIN_LEFT:
        if (parse_len(v, vl, &a->lc, &l, 1)) st->margin[prop - PR_MARGIN_TOP] = l;
        return;
    case PR_PADDING: apply_box4(a, st->padding, v, vl, 0); return;
    case PR_PADDING_INLINE: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 0); st->padding[1] = b4[1]; st->padding[3] = b4[3]; return; }
    case PR_PADDING_BLOCK: { struct wlen b4[4]; apply_box4(a, b4, v, vl, 0); st->padding[0] = b4[0]; st->padding[2] = b4[2]; return; }
    case PR_PADDING_TOP: case PR_PADDING_RIGHT: case PR_PADDING_BOTTOM: case PR_PADDING_LEFT:
        if (parse_len(v, vl, &a->lc, &l, 1) && l.kind == LK_VAL && l.px >= 0) st->padding[prop - PR_PADDING_TOP] = l;
        return;
    case PR_BOX_SIZING: st->box_sizing = is_kw(v, vl, "border-box"); return;
    case PR_BORDER: apply_border_one(a, st, 15, v, vl); return;
    case PR_BORDER_TOP: apply_border_one(a, st, 1, v, vl); return;
    case PR_BORDER_RIGHT: apply_border_one(a, st, 2, v, vl); return;
    case PR_BORDER_BOTTOM: apply_border_one(a, st, 4, v, vl); return;
    case PR_BORDER_LEFT: apply_border_one(a, st, 8, v, vl); return;
    case PR_BORDER_WIDTH: case PR_BORDER_TOP_WIDTH: case PR_BORDER_RIGHT_WIDTH: case PR_BORDER_BOTTOM_WIDTH: case PR_BORDER_LEFT_WIDTH: {
        struct wlen b4[4]; int w[4]; int n = 0; const char *p = v, *e = v + vl;
        while (n < 4 && (p = next_tok(p, e, &ts, &tl))) { int px = 3; if (is_kw(ts, tl, "thin")) px = 1; else if (is_kw(ts, tl, "medium")) px = 3; else if (is_kw(ts, tl, "thick")) px = 5; else if (parse_len(ts, tl, &a->lc, &b4[0], 0) && b4[0].kind == LK_VAL) px = b4[0].px; w[n++] = px; }
        if (!n) return;
        int bw[4] = { w[0], n > 1 ? w[1] : w[0], n > 2 ? w[2] : w[0], n > 3 ? w[3] : (n > 1 ? w[1] : w[0]) };
        if (prop == PR_BORDER_WIDTH) { for (int i = 0; i < 4; i++) if (st->border_style[i]) st->border_w[i] = (int16_t)bw[i]; }
        else { int i = prop - PR_BORDER_TOP_WIDTH; if (st->border_style[i]) st->border_w[i] = (int16_t)bw[0]; }
        return; }
    case PR_BORDER_STYLE: case PR_BORDER_TOP_STYLE: case PR_BORDER_RIGHT_STYLE: case PR_BORDER_BOTTOM_STYLE: case PR_BORDER_LEFT_STYLE: {
        int s[4]; int n = 0; const char *p = v, *e = v + vl;
        while (n < 4 && (p = next_tok(p, e, &ts, &tl))) { int b = border_style_of(ts, tl); if (b < 0) return; s[n++] = b; }
        if (!n) return;
        int bs[4] = { s[0], n > 1 ? s[1] : s[0], n > 2 ? s[2] : s[0], n > 3 ? s[3] : (n > 1 ? s[1] : s[0]) };
        for (int i = 0; i < 4; i++) {
            if (prop != PR_BORDER_STYLE && i != prop - PR_BORDER_TOP_STYLE) continue;
            int sv = prop == PR_BORDER_STYLE ? bs[i] : bs[0];
            st->border_style[i] = (uint8_t)sv; if (sv && st->border_w[i] == 0) st->border_w[i] = 3; if (!sv) st->border_w[i] = 0;
        }
        return; }
    case PR_BORDER_COLOR: case PR_BORDER_TOP_COLOR: case PR_BORDER_RIGHT_COLOR: case PR_BORDER_BOTTOM_COLOR: case PR_BORDER_LEFT_COLOR: {
        uint32_t c[4]; int n = 0; const char *p = v, *e = v + vl;
        while (n < 4 && (p = next_tok(p, e, &ts, &tl))) { c[n] = tok_color(ts, tl, st->color, &ok); if (!ok) return; n++; }
        if (!n) return;
        uint32_t bc[4] = { c[0], n > 1 ? c[1] : c[0], n > 2 ? c[2] : c[0], n > 3 ? c[3] : (n > 1 ? c[1] : c[0]) };
        for (int i = 0; i < 4; i++) { if (prop == PR_BORDER_COLOR) st->border_color[i] = bc[i]; else if (i == prop - PR_BORDER_TOP_COLOR) st->border_color[i] = bc[0]; }
        return; }
    case PR_BORDER_RADIUS: case PR_TOP_LEFT_RADIUS: case PR_TOP_RIGHT_RADIUS: case PR_BOTTOM_LEFT_RADIUS: case PR_BOTTOM_RIGHT_RADIUS: {
        const char *p = next_tok(v, v + vl, &ts, &tl); (void)p;
        if (parse_len(ts, tl, &a->lc, &l, 0) && l.kind == LK_VAL) { int r = l.px; if (l.pct >= 4900) r = 999; if (r > 999) r = 999; if (r < 0) r = 0; if (prop == PR_BORDER_RADIUS || r > st->radius) st->radius = (int16_t)r; }
        return; }
    case PR_OPACITY: { char b[16]; if (vl >= sizeof(b)) return; memcpy(b, v, vl); b[vl] = 0; const char *q = b; int32_t nv; if (!parse_num(&q, &nv)) return;
        if (*q == '%') nv *= 10;
        nv = nv < 0 ? 0 : (nv > 1000 ? 1000 : nv); st->opacity = (uint8_t)(nv * 255 / 1000); return; }
    case PR_FLEX: apply_flex(a, st, v, vl); return;
    case PR_FLEX_GROW: { char b[16]; if (vl >= sizeof(b)) return; memcpy(b, v, vl); b[vl] = 0; const char *q = b; int32_t nv; if (parse_num(&q, &nv)) st->flex_grow = (int16_t)(nv / 10); return; }
    case PR_FLEX_SHRINK: { char b[16]; if (vl >= sizeof(b)) return; memcpy(b, v, vl); b[vl] = 0; const char *q = b; int32_t nv; if (parse_num(&q, &nv)) st->flex_shrink = (int16_t)(nv / 10); return; }
    case PR_FLEX_BASIS: if (parse_len(v, vl, &a->lc, &l, 0)) st->flex_basis = l; return;
    case PR_FLEX_DIRECTION: st->flex_dir = is_kw(v, vl, "row-reverse") ? 1 : is_kw(v, vl, "column") ? 2 : is_kw(v, vl, "column-reverse") ? 3 : 0; return;
    case PR_FLEX_WRAP: st->flex_wrap = !is_kw(v, vl, "nowrap"); return;
    case PR_FLEX_FLOW: { const char *p = v, *e = v + vl; while ((p = next_tok(p, e, &ts, &tl))) {
            if (is_kw(ts, tl, "row")) st->flex_dir = 0; else if (is_kw(ts, tl, "row-reverse")) st->flex_dir = 1; else if (is_kw(ts, tl, "column")) st->flex_dir = 2; else if (is_kw(ts, tl, "column-reverse")) st->flex_dir = 3;
            else if (is_kw(ts, tl, "nowrap")) st->flex_wrap = 0; else if (is_kw(ts, tl, "wrap") || is_kw(ts, tl, "wrap-reverse")) st->flex_wrap = 1; } return; }
    case PR_JUSTIFY_CONTENT: { int al = align_of(v, vl); if (al < 0) return;
        st->justify = (uint8_t)(al >= 10 ? al - 10 : (al == AI_START ? JC_START : al == AI_END ? JC_END : al == AI_CENTER ? JC_CENTER : JC_START)); return; }
    case PR_ALIGN_ITEMS: { int al = align_of(v, vl); if (al >= 0 && al < 10) st->align_items = (uint8_t)al; return; }
    case PR_PLACE_ITEMS: { int al = align_of(v, vl); if (al >= 0 && al < 10) st->align_items = (uint8_t)al; return; }
    case PR_ALIGN_SELF: { int al = align_of(v, vl); if (al >= 0 && al < 10) st->align_self = (uint8_t)al; return; }
    case PR_ALIGN_CONTENT: { int al = align_of(v, vl); if (al >= 0) st->align_content = (uint8_t)(al >= 10 ? al - 10 : al); return; }
    case PR_ORDER: st->order = (int16_t)wu_atoi(v); return;
    case PR_GAP: case PR_GRID_GAP: { const char *p = next_tok(v, v + vl, &ts, &tl); if (!p) return;
        if (!parse_len(ts, tl, &a->lc, &l, 0)) return;
        st->gap_r = l; st->gap_c = l;
        if (next_tok(p, v + vl, &ts, &tl) && parse_len(ts, tl, &a->lc, &l, 0)) st->gap_c = l;
        return; }
    case PR_ROW_GAP: if (parse_len(v, vl, &a->lc, &l, 0)) st->gap_r = l; return;
    case PR_COLUMN_GAP: if (parse_len(v, vl, &a->lc, &l, 0)) st->gap_c = l; return;
    case PR_GRID_TEMPLATE_COLUMNS: apply_grid_cols(a, st, v, vl); return;
    case PR_GRID_COLUMN: { const char *q = findn(v, vl, "span"); if (q) { st->col_span = (int16_t)wu_atoi(q + 4); } else st->col_span = 1; return; }
    case PR_COLOR: { uint32_t c = tok_color(v, vl, par->color, &ok); if (ok) st->color = c; return; }
    case PR_FONT_SIZE: { int px; if (font_size_of(a, v, vl, &px)) { st->font_px = (int16_t)(px < 1 ? 1 : (px > 400 ? 400 : px)); a->lc.font_px = st->font_px; } return; }
    case PR_FONT_WEIGHT: if (is_kw(v, vl, "bold") || is_kw(v, vl, "bolder")) st->bold = 1; else if (is_kw(v, vl, "normal") || is_kw(v, vl, "lighter")) st->bold = 0; else st->bold = wu_atoi(v) >= 600; return;
    case PR_FONT_STYLE: st->italic = is_kw(v, vl, "italic") || is_kw(v, vl, "oblique"); return;
    case PR_FONT_FAMILY: st->mono = (uint8_t)family_is_mono(v, vl); return;
    case PR_FONT: apply_font_shorthand(a, st, v, vl); return;
    case PR_LINE_HEIGHT: apply_lh(a, st, v, vl); return;
    case PR_TEXT_ALIGN: st->text_align = is_kw(v, vl, "center") || is_kw(v, vl, "-webkit-center") ? TA_CENTER : is_kw(v, vl, "right") || is_kw(v, vl, "end") ? TA_RIGHT : is_kw(v, vl, "justify") ? TA_JUSTIFY : TA_LEFT; return;
    case PR_TEXT_DECORATION: case PR_TEXT_DECORATION_LINE: {
        uint8_t d = 0; const char *p = v, *e = v + vl;
        while ((p = next_tok(p, e, &ts, &tl))) { if (is_kw(ts, tl, "underline")) d |= 1; else if (is_kw(ts, tl, "line-through")) d |= 2; else if (is_kw(ts, tl, "overline")) d |= 4; }
        st->text_decor = d; return; }
    case PR_TEXT_TRANSFORM: st->text_transform = is_kw(v, vl, "uppercase") ? 1 : is_kw(v, vl, "lowercase") ? 2 : is_kw(v, vl, "capitalize") ? 3 : 0; return;
    case PR_TEXT_INDENT: if (parse_len(v, vl, &a->lc, &l, 0)) st->text_indent = l; return;
    case PR_LETTER_SPACING: if (parse_len(v, vl, &a->lc, &l, 0)) st->letter_sp = (int16_t)(l.kind == LK_VAL ? l.px : 0); return;
    case PR_WHITE_SPACE: st->white_space = is_kw(v, vl, "nowrap") ? WS_NOWRAP : is_kw(v, vl, "pre") ? WS_PRE : is_kw(v, vl, "pre-wrap") || is_kw(v, vl, "break-spaces") ? WS_PRE_WRAP : is_kw(v, vl, "pre-line") ? WS_PRE_LINE : WS_NORMAL; return;
    case PR_VERTICAL_ALIGN: st->vertical_align = is_kw(v, vl, "top") ? VA_TOP : is_kw(v, vl, "middle") ? VA_MIDDLE : is_kw(v, vl, "bottom") ? VA_BOTTOM : is_kw(v, vl, "sub") ? VA_SUB :
        is_kw(v, vl, "super") ? VA_SUPER : is_kw(v, vl, "text-top") ? VA_TEXT_TOP : is_kw(v, vl, "text-bottom") ? VA_TEXT_BOTTOM : VA_BASELINE; return;
    case PR_TEXT_OVERFLOW: st->text_overflow = is_kw(v, vl, "ellipsis"); return;
    case PR_WORD_BREAK: st->word_break = is_kw(v, vl, "break-all") || is_kw(v, vl, "break-word") ? 1 : 0; return;
    case PR_OVERFLOW_WRAP: case PR_WORD_WRAP: st->word_break = (is_kw(v, vl, "break-word") || is_kw(v, vl, "anywhere")) ? 2 : (st->word_break == 1 ? 1 : 0); return;
    case PR_LIST_STYLE: case PR_LIST_STYLE_TYPE: case PR_LIST_STYLE_POSITION: {
        const char *p = v, *e = v + vl;
        while ((p = next_tok(p, e, &ts, &tl))) {
            if (is_kw(ts, tl, "none")) st->list_style = LS_NONE; else if (is_kw(ts, tl, "disc")) st->list_style = LS_DISC; else if (is_kw(ts, tl, "circle")) st->list_style = LS_CIRCLE;
            else if (is_kw(ts, tl, "square")) st->list_style = LS_SQUARE; else if (is_kw(ts, tl, "decimal") || is_kw(ts, tl, "decimal-leading-zero")) st->list_style = LS_DECIMAL;
            else if (is_kw(ts, tl, "lower-alpha") || is_kw(ts, tl, "lower-latin")) st->list_style = LS_ALPHA; else if (is_kw(ts, tl, "upper-alpha") || is_kw(ts, tl, "upper-latin")) st->list_style = LS_UALPHA;
            else if (is_kw(ts, tl, "lower-roman") || is_kw(ts, tl, "upper-roman")) st->list_style = LS_ROMAN;
            else if (is_kw(ts, tl, "inside")) st->list_inside = 1; else if (is_kw(ts, tl, "outside")) st->list_inside = 0;
        }
        return; }
    case PR_BACKGROUND: {
        st->bg_color = 0; st->bg_image = 0; st->has_grad = 0; st->bg_repeat = 0; st->bg_size_mode = 0; st->bg_pos_x = 0; st->bg_pos_y = 0;
        const char *p = v, *e = v + vl;
        while ((p = next_tok(p, e, &ts, &tl))) apply_bg_token(a, st, ts, tl, a->c);
        return; }
    case PR_BACKGROUND_COLOR: { uint32_t c = tok_color(v, vl, st->color, &ok); if (ok) st->bg_color = c; return; }
    case PR_BACKGROUND_IMAGE: { st->bg_image = 0; st->has_grad = 0; const char *p = v, *e = v + vl; while ((p = next_tok(p, e, &ts, &tl))) { if (ts[0] != ',') apply_bg_token(a, st, ts, tl, a->c); } return; }
    case PR_BACKGROUND_REPEAT: case PR_BACKGROUND_SIZE: case PR_BACKGROUND_POSITION: case PR_BACKGROUND_POSITION_X: {
        const char *p = v, *e = v + vl; int idx = 0;
        while ((p = next_tok(p, e, &ts, &tl))) {
            apply_bg_token(a, st, ts, tl, a->c);
            if (prop == PR_BACKGROUND_SIZE && parse_len(ts, tl, &a->lc, &l, 0) && l.kind != LK_NONE) { if (idx == 0) { st->bg_w = l; st->bg_h = l; st->bg_h.kind = LK_AUTO; st->bg_size_mode = 3; } else st->bg_h = l; idx++; }
            if (prop == PR_BACKGROUND_POSITION && parse_len(ts, tl, &a->lc, &l, 0) && l.kind == LK_VAL) { int pv = l.pct ? l.pct / 100 : 0; if (idx == 0) st->bg_pos_x = (int16_t)pv; else st->bg_pos_y = (int16_t)pv; idx++; }
        }
        return; }
    case PR_BORDER_COLLAPSE: st->border_collapse = is_kw(v, vl, "collapse"); return;
    case PR_BORDER_SPACING: if (parse_len(v, vl, &a->lc, &l, 0) && l.kind == LK_VAL) { st->spacing_h = (int16_t)l.px; st->spacing_v = (int16_t)l.px;
            const char *p = next_tok(v, v + vl, &ts, &tl); if (p && next_tok(p, v + vl, &ts, &tl) && parse_len(ts, tl, &a->lc, &l, 0)) st->spacing_v = (int16_t)l.px; } return;
    case PR_CURSOR: st->pointer = is_kw(v, vl, "pointer"); return;
    case PR_BOX_SHADOW: {
        /* SOMA_PARCHE_028_UI: primera sombra exterior de la lista (las interiores, "inset", no se dibujan) */
        const char *p = v, *e = v + vl; int nl = 0, vals[4] = { 0, 0, 0, 0 }, inset = 0, got = 0; uint32_t col = 0x59000000u;
        st->shadow_color = 0;
        while ((p = next_tok(p, e, &ts, &tl))) {
            if (tl == 1 && ts[0] == ',') { if (!inset && got) break; nl = 0; inset = 0; got = 0; vals[0] = vals[1] = vals[2] = vals[3] = 0; col = 0x59000000u; continue; }
            if (is_kw(ts, tl, "none")) return;
            if (is_kw(ts, tl, "inset")) { inset = 1; continue; }
            int ok; uint32_t c2 = tok_color(ts, tl, st->color, &ok);
            if (ok) { col = c2; got = 1; continue; }
            if (nl < 4 && parse_len(ts, tl, &a->lc, &l, 1) && l.kind == LK_VAL) { vals[nl++] = l.px; got = 1; }
        }
        if (inset || nl < 2 || !(col >> 24)) return;
        for (int i = 0; i < 4; i++) { if (vals[i] > 160) vals[i] = 160; if (vals[i] < -160) vals[i] = -160; }
        if (vals[2] < 0) vals[2] = 0;
        st->shadow_x = (int16_t)vals[0]; st->shadow_y = (int16_t)vals[1]; st->shadow_blur = (int16_t)vals[2]; st->shadow_spread = (int16_t)vals[3]; st->shadow_color = col;
        return; }
    default: break;
    }
    (void)cname; (void)cnl;
}

/* ============================ selectores ============================ */
#define PF_FIRST 1u
#define PF_LAST 2u
#define PF_ONLY 4u
#define PF_FIRST_T 8u
#define PF_LAST_T 16u
#define PF_ONLY_T 32u
#define PF_ROOT 64u
#define PF_EMPTY 128u
#define PF_LINK 256u
#define PF_HOVER 512u
#define PF_FOCUS 1024u
#define PF_CHECKED 2048u
#define PF_DISABLED 4096u
#define PF_ENABLED 8192u
#define PF_NEVER 16384u
#define PF_NTH 32768u

struct wattrsel { const char *n; const char *v; uint8_t op; uint8_t ci; };
struct wsimple {
    uint8_t comb, tag, ncls, nattr, nth_kind;
    const char *name, *id, *cls[3];
    struct wattrsel at[2];
    uint32_t pseudo;
    int16_t nth_a, nth_b;
    struct wsimple *not_;
};
struct wsel { struct wsimple *parts; uint8_t n; uint32_t spec; uint8_t pe; };

static int is_ident_ch(int c) { return wu_isalnum(c) || c == '-' || c == '_' || (unsigned char)c >= 0x80 || c == '\\'; }

static const char *read_ident(const char *p, const char *e, char *out, uint32_t max)
{
    uint32_t n = 0;
    while (p < e && is_ident_ch(*p)) {
        if (*p == '\\' && p + 1 < e) p++;
        if (n + 1 < max) out[n++] = *p;
        p++;
    }
    out[n] = 0; return p;
}

static int parse_nth(const char *s, int16_t *a, int16_t *b)
{
    char t[24]; uint32_t n = 0; for (; *s && n < 23; s++) if (!wu_isspace(*s)) t[n++] = (char)wu_lower(*s); t[n] = 0;
    if (wu_streq(t, "odd")) { *a = 2; *b = 1; return 1; }
    if (wu_streq(t, "even")) { *a = 2; *b = 0; return 1; }
    const char *p = t; int sign = 1; int av = 0, bv = 0, hasn = 0;
    const char *q = p; if (*q == '-') { sign = -1; q++; } else if (*q == '+') q++;
    int num = 0, anynum = 0; while (wu_isdigit(*q)) { num = num * 10 + (*q - '0'); q++; anynum = 1; }
    if (*q == 'n') { hasn = 1; av = sign * (anynum ? num : 1); q++;
        if (*q == '+' || *q == '-') { int s2 = *q == '-' ? -1 : 1; q++; int nb = 0; while (wu_isdigit(*q)) { nb = nb * 10 + (*q - '0'); q++; } bv = s2 * nb; }
    } else if (anynum) { bv = sign * num; } else return 0;
    if (*q) return 0;
    *a = (int16_t)(hasn ? av : 0); *b = (int16_t)bv; return 1;
}

static const char *parse_compound(struct wcss *c, const char *p, const char *e, struct wsimple *s, uint32_t *spec, int in_not)
{
    char id[64]; int any = 0;
    memset(s, 0, sizeof(*s));
    for (;;) {
        if (p >= e) break;
        char ch = *p;
        if (ch == '*') { p++; any = 1; continue; }
        if (is_ident_ch(ch) && !any && !s->tag && !s->name && !s->id && !s->ncls && !s->nattr && !s->pseudo) {
            p = read_ident(p, e, id, sizeof(id)); any = 1;
            for (char *q = id; *q; q++) *q = (char)wu_lower(*q);
            int t = wd_tag_of(id, wu_strlen(id));
            if (t) s->tag = (uint8_t)t; else s->name = wa_strdup(&c->d->ar, id);
            *spec += 1; continue;
        }
        if (ch == '#') { p = read_ident(p + 1, e, id, sizeof(id)); s->id = wa_strdup(&c->d->ar, id); *spec += 0x10000; any = 1; continue; }
        if (ch == '.') { p = read_ident(p + 1, e, id, sizeof(id)); if (s->ncls < 3) s->cls[s->ncls++] = wa_strdup(&c->d->ar, id); *spec += 0x100; any = 1; continue; }
        if (ch == '[') {
            p++; while (p < e && wu_isspace(*p)) p++;
            char nm[40]; p = read_ident(p, e, nm, sizeof(nm)); while (p < e && wu_isspace(*p)) p++;
            struct wattrsel at; memset(&at, 0, sizeof(at));
            for (char *q = nm; *q; q++) *q = (char)wu_lower(*q);
            at.n = wa_strdup(&c->d->ar, nm);
            if (p < e && *p != ']') {
                uint8_t op = '=';
                if (*p != '=') { op = (uint8_t)*p; p++; }
                if (p >= e || *p != '=') return 0;
                p++; while (p < e && wu_isspace(*p)) p++;
                char val[96]; uint32_t n = 0;
                if (p < e && (*p == '"' || *p == '\'')) { char q = *p++; while (p < e && *p != q) { if (n < 95) val[n++] = *p; p++; } if (p < e) p++; }
                else { while (p < e && *p != ']' && !wu_isspace(*p)) { if (n < 95) val[n++] = *p; p++; } }
                val[n] = 0; at.v = wa_strdup(&c->d->ar, val); at.op = op;
                while (p < e && wu_isspace(*p)) p++;
                if (p < e && (*p == 'i' || *p == 'I')) { at.ci = 1; p++; }
            }
            while (p < e && *p != ']') p++;
            if (p >= e) return 0;
            p++;
            if (s->nattr < 2) s->at[s->nattr++] = at;
            *spec += 0x100; any = 1; continue;
        }
        if (ch == ':') {
            int dbl = (p + 1 < e && p[1] == ':');
            char nm[32]; p = read_ident(p + 1 + dbl, e, nm, sizeof(nm));
            for (char *q = nm; *q; q++) *q = (char)wu_lower(*q);
            if (wu_streq(nm, "before") || wu_streq(nm, "after")) { c->pe_seen = 1; any = 1; continue; }
            if (dbl) return 0;
            const char *arg = 0; uint32_t argl = 0;
            if (p < e && *p == '(') {
                int d = 1; const char *a0 = ++p;
                while (p < e && d) { if (*p == '(') d++; else if (*p == ')') d--; if (d) p++; }
                arg = a0; argl = (uint32_t)(p - a0); if (p < e) p++;
            }
            *spec += 0x100; any = 1;
            if (wu_streq(nm, "first-child")) s->pseudo |= PF_FIRST; else if (wu_streq(nm, "last-child")) s->pseudo |= PF_LAST;
            else if (wu_streq(nm, "only-child")) s->pseudo |= PF_ONLY; else if (wu_streq(nm, "first-of-type")) s->pseudo |= PF_FIRST_T;
            else if (wu_streq(nm, "last-of-type")) s->pseudo |= PF_LAST_T; else if (wu_streq(nm, "only-of-type")) s->pseudo |= PF_ONLY_T;
            else if (wu_streq(nm, "root")) s->pseudo |= PF_ROOT; else if (wu_streq(nm, "empty")) s->pseudo |= PF_EMPTY;
            else if (wu_streq(nm, "link") || wu_streq(nm, "any-link")) s->pseudo |= PF_LINK;
            else if (wu_streq(nm, "hover")) s->pseudo |= PF_HOVER; else if (wu_streq(nm, "focus") || wu_streq(nm, "focus-visible") || wu_streq(nm, "focus-within")) s->pseudo |= PF_FOCUS;
            else if (wu_streq(nm, "checked")) s->pseudo |= PF_CHECKED; else if (wu_streq(nm, "disabled")) s->pseudo |= PF_DISABLED; else if (wu_streq(nm, "enabled")) s->pseudo |= PF_ENABLED;
            else if (wu_streq(nm, "visited") || wu_streq(nm, "active") || wu_streq(nm, "target") || wu_streq(nm, "invalid") || wu_streq(nm, "placeholder-shown") || wu_streq(nm, "read-only") || wu_streq(nm, "out-of-range") || wu_streq(nm, "indeterminate")) s->pseudo |= PF_NEVER;
            else if (wu_streq(nm, "required") || wu_streq(nm, "optional") || wu_streq(nm, "valid") || wu_streq(nm, "in-range") || wu_streq(nm, "read-write") || wu_streq(nm, "defined") || wu_streq(nm, "scope")) { /* aceptadas sin efecto */ }
            else if (wu_streq(nm, "nth-child") || wu_streq(nm, "nth-last-child") || wu_streq(nm, "nth-of-type") || wu_streq(nm, "nth-last-of-type")) {
                char tmp[32]; if (!arg || argl >= sizeof(tmp)) return 0; memcpy(tmp, arg, argl); tmp[argl] = 0;
                char *of = (char *)wu_find(tmp, " of "); if (of) *of = 0;
                if (!parse_nth(tmp, &s->nth_a, &s->nth_b)) return 0;
                s->pseudo |= PF_NTH; s->nth_kind = (uint8_t)(wu_streq(nm, "nth-child") ? 1 : wu_streq(nm, "nth-last-child") ? 2 : wu_streq(nm, "nth-of-type") ? 3 : 4);
            } else if (wu_streq(nm, "not") || wu_streq(nm, "is") || wu_streq(nm, "where") || wu_streq(nm, "matches")) {
                if (!arg || in_not) return 0;
                struct wsimple *ns = (struct wsimple *)wa_zalloc(&c->d->ar, sizeof(*ns)); if (!ns) return 0;
                uint32_t sp2 = 0; const char *q = arg, *qe = arg + argl; while (q < qe && wu_isspace(*q)) q++;
                const char *r = parse_compound(c, q, qe, ns, &sp2, 1);
                if (!r) return 0;
                while (r < qe && wu_isspace(*r)) r++;
                if (r != qe) return 0;         /* listas con comas / combinadores no soportadas */
                if (wu_streq(nm, "not")) { s->not_ = ns; *spec += sp2; }
                else { /* :is / :where : fusionar en este compuesto si este esta vacio */
                    if (s->tag || s->name || s->id || s->ncls || s->nattr || s->pseudo) return 0;
                    *s = *ns; if (wu_streq(nm, "is")) *spec += sp2; *spec -= 0x100;
                }
            } else return 0;
            continue;
        }
        break;
    }
    return any ? p : 0;
}

static struct wsel *compile_selector(struct wcss *c, const char *s, uint32_t n)
{
    struct wsimple tmp[16]; uint8_t combs[16]; int cnt = 0; uint32_t spec = 0;
    const char *p = s, *e = s + n; char pend = 0;
    c->pe_seen = 0;
    while (p < e && wu_isspace(*p)) p++;
    while (p < e) {
        if (cnt >= 16) return 0;
        const char *r = parse_compound(c, p, e, &tmp[cnt], &spec, 0);
        if (!r) return 0;
        combs[cnt] = (uint8_t)(cnt == 0 ? 0 : pend);
        cnt++; p = r;
        pend = 0; int ws = 0;
        while (p < e && wu_isspace(*p)) { p++; ws = 1; }
        if (p >= e) break;
        if (*p == '>' || *p == '+' || *p == '~') { pend = *p++; while (p < e && wu_isspace(*p)) p++; }
        else if (ws) pend = ' ';
        else return 0;
        if (p >= e) return 0;
    }
    if (!cnt) return 0;
    struct wsel *sel = (struct wsel *)wa_zalloc(&c->d->ar, sizeof(*sel));
    struct wsimple *parts = (struct wsimple *)wa_zalloc(&c->d->ar, sizeof(struct wsimple) * (uint32_t)cnt);
    if (!sel || !parts) return 0;
    for (int i = 0; i < cnt; i++) { parts[i] = tmp[cnt - 1 - i]; parts[i].comb = combs[cnt - 1 - i]; }
    sel->parts = parts; sel->n = (uint8_t)cnt; sel->spec = spec; sel->pe = (uint8_t)c->pe_seen;
    return sel;
}

static struct wnode *prev_elem(struct wnode *n) { struct wnode *p = n->prev; while (p && p->type != WN_ELEM) p = p->prev; return p; }
static struct wnode *next_elem(struct wnode *n) { struct wnode *p = n->next; while (p && p->type != WN_ELEM) p = p->next; return p; }

static int attr_match(const struct wattrsel *a, const struct wnode *n)
{
    const char *v = wd_attr(n, a->n);
    if (!v) return 0;
    if (!a->op) return 1;
    uint32_t vl = wu_strlen(v), al = wu_strlen(a->v);
    int ci = a->ci;
#define EQN(x, y, k) (ci ? wu_strnieq(x, y, k) : wu_strneq(x, y, k))
    switch (a->op) {
    case '=': return vl == al && EQN(v, a->v, al);
    case '^': return al && vl >= al && EQN(v, a->v, al);
    case '$': return al && vl >= al && EQN(v + vl - al, a->v, al);
    case '*': { if (!al) return 0; for (uint32_t i = 0; i + al <= vl; i++) if (EQN(v + i, a->v, al)) return 1; return 0; }
    case '~': { const char *p = v; while (*p) { while (wu_isspace(*p)) p++; const char *s = p; while (*p && !wu_isspace(*p)) p++; if ((uint32_t)(p - s) == al && al && EQN(s, a->v, al)) return 1; } return 0; }
    case '|': return vl >= al && EQN(v, a->v, al) && (vl == al || v[al] == '-');
    }
#undef EQN
    return 0;
}

static int nth_ok(int idx, int a, int b) { if (a == 0) return idx == b; int d = idx - b; return d % a == 0 && d / a >= 0; }

static int match_simple(const struct wsimple *s, struct wnode *n)
{
    if (s->tag) { if (n->tag != s->tag) return 0; }
    else if (s->name && !wu_streq(n->name, s->name)) return 0;
    if (s->id) { const char *v = wd_attr(n, "id"); if (!v || !wu_streq(v, s->id)) return 0; }
    for (int i = 0; i < s->ncls; i++) if (!wd_has_class(n, s->cls[i])) return 0;
    for (int i = 0; i < s->nattr; i++) if (!attr_match(&s->at[i], n)) return 0;
    uint32_t pf = s->pseudo;
    if (pf) {
        if (pf & PF_NEVER) return 0;
        if ((pf & PF_FIRST) && prev_elem(n)) return 0;
        if ((pf & PF_LAST) && next_elem(n)) return 0;
        if ((pf & PF_ONLY) && (prev_elem(n) || next_elem(n))) return 0;
        if (pf & (PF_FIRST_T | PF_LAST_T | PF_ONLY_T)) {
            int before = 0, after = 0;
            for (struct wnode *q = n->prev; q; q = q->prev) if (q->type == WN_ELEM && q->tag == n->tag && wu_streq(q->name, n->name)) before = 1;
            for (struct wnode *q = n->next; q; q = q->next) if (q->type == WN_ELEM && q->tag == n->tag && wu_streq(q->name, n->name)) after = 1;
            if ((pf & PF_FIRST_T) && before) return 0;
            if ((pf & PF_LAST_T) && after) return 0;
            if ((pf & PF_ONLY_T) && (before || after)) return 0;
        }
        if ((pf & PF_ROOT) && n->tag != WT_HTML) return 0;
        if ((pf & PF_EMPTY)) { for (struct wnode *q = n->first; q; q = q->next) if (q->type == WN_ELEM || (q->type == WN_TEXT && q->tlen)) return 0; }
        if ((pf & PF_LINK) && !((n->tag == WT_A || n->tag == WT_AREA) && wd_attr(n, "href"))) return 0;
        if ((pf & PF_HOVER) && !(n->flags & WNF_HOVER)) return 0;
        if ((pf & PF_FOCUS) && !(n->flags & WNF_FOCUS)) return 0;
        if ((pf & PF_CHECKED) && !((n->flags & WNF_CHECKED) || (n->tag == WT_OPTION && wd_attr(n, "selected")))) return 0;
        if ((pf & PF_DISABLED) && !(wd_attr(n, "disabled") || (n->flags & WNF_DISABLED))) return 0;
        if ((pf & PF_ENABLED) && (wd_attr(n, "disabled") || (n->flags & WNF_DISABLED))) return 0;
        if (pf & PF_NTH) {
            int idx = 1, k = s->nth_kind;
            if (k == 1 || k == 3) { for (struct wnode *q = n->prev; q; q = q->prev) if (q->type == WN_ELEM && (k == 1 || (q->tag == n->tag && wu_streq(q->name, n->name)))) idx++; }
            else { for (struct wnode *q = n->next; q; q = q->next) if (q->type == WN_ELEM && (k == 2 || (q->tag == n->tag && wu_streq(q->name, n->name)))) idx++; }
            if (!nth_ok(idx, s->nth_a, s->nth_b)) return 0;
        }
    }
    if (s->not_ && match_simple(s->not_, n)) return 0;
    return 1;
}

static int match_from(const struct wsel *sel, int i, struct wnode *n)
{
    if (!match_simple(&sel->parts[i], n)) return 0;
    if (i == sel->n - 1) return 1;
    uint8_t cb = sel->parts[i].comb;
    struct wnode *q;
    switch (cb) {
    case '>': q = n->parent; return q && q->type == WN_ELEM && match_from(sel, i + 1, q);
    case '+': q = prev_elem(n); return q && match_from(sel, i + 1, q);
    case '~': for (q = prev_elem(n); q; q = prev_elem(q)) if (match_from(sel, i + 1, q)) return 1; return 0;
    default: for (q = n->parent; q && q->type == WN_ELEM; q = q->parent) if (match_from(sel, i + 1, q)) return 1; return 0;
    }
}

/* ============================ hojas ============================ */
static int add_rule_ptr(struct wcss *c, struct wrule *r)
{
    if (c->nrules >= c->cap) {
        int nc = c->cap ? c->cap * 2 : 256;
        struct wrule **nr = (struct wrule **)kmalloc(sizeof(*nr) * (uint32_t)nc); if (!nr) return 0;
        if (c->rules) { memcpy(nr, c->rules, sizeof(*nr) * (uint32_t)c->nrules); kfree(c->rules); }
        c->rules = nr; c->cap = nc;
    }
    c->rules[c->nrules++] = r; return 1;
}

struct wcss *wc_new(struct wdoc *d)
{
    struct wcss *c = (struct wcss *)kmalloc(sizeof(*c)); if (!c) return 0;
    memset(c, 0, sizeof(*c)); c->d = d;
    c->match_cap = 1024; c->match = (struct wrule **)kmalloc(sizeof(struct wrule *) * 1024);
    c->scratch = (char *)kmalloc(2048);
    if (!c->match || !c->scratch) { wc_free(c); return 0; }
    return c;
}
void wc_free(struct wcss *c)
{
    if (!c) return;
    if (c->rules) kfree(c->rules);
    if (c->match) kfree(c->match);
    if (c->scratch) kfree(c->scratch);
    if (c->imports) kfree(c->imports);
    kfree(c);
}

static const char *skip_to_block_end(const char *p, const char *e)   /* p apunta tras '{'; devuelve el '}' que cierra */
{
    int d = 1;
    while (p < e) {
        if (*p == '"' || *p == '\'') { char q = *p++; while (p < e && *p != q) { if (*p == '\\') p++; p++; } if (p < e) p++; continue; }
        if (*p == '{') d++;
        else if (*p == '}') { d--; if (!d) return p; }
        p++;
    }
    return e;
}

static int parse_decls(struct wcss *c, const char *s, const char *e, struct wdecl *out, int max)
{
    int n = 0; const char *p = s;
    while (p < e && n < max) {
        while (p < e && (wu_isspace(*p) || *p == ';')) p++;
        if (p >= e) break;
        const char *ns = p; while (p < e && *p != ':' && *p != ';') p++;
        if (p >= e || *p != ':') { while (p < e && *p != ';') p++; continue; }
        const char *ne = p; while (ne > ns && wu_isspace(ne[-1])) ne--;
        p++;
        const char *vs = p; int depth = 0;
        while (p < e) {
            if (*p == '"' || *p == '\'') { char q = *p++; while (p < e && *p != q) { if (*p == '\\') p++; p++; } if (p < e) p++; continue; }
            if (*p == '(') depth++; else if (*p == ')') { if (depth) depth--; }
            else if (*p == ';' && !depth) break;
            p++;
        }
        const char *ve = p; while (ve > vs && wu_isspace(ve[-1])) ve--;
        int imp = 0;
        if (ve - vs >= 10) { const char *b = ve - 1; while (b > vs && wu_isspace(*b)) b--; if (b - vs >= 8 && wu_strnieq(b - 8, "important", 9)) { const char *q = b - 9; while (q > vs && wu_isspace(*q)) q--; if (*q == '!') { imp = 1; ve = q; while (ve > vs && wu_isspace(ve[-1])) ve--; } } }
        int pid = prop_id(ns, (uint32_t)(ne - ns));
        if (!pid) continue;
        out[n].prop = (uint16_t)pid; out[n].imp = (uint8_t)imp; out[n].val = vs; out[n].vlen = (uint16_t)(ve - vs > 65000 ? 65000 : ve - vs);
        out[n].name = pid == PR_CUSTOM ? ns : 0; out[n].nlen = (uint16_t)(ne - ns);
        c = c; n++;
    }
    return n;
}

static void media_eval(struct wcss *c, const char *s, uint32_t n, int *minw, int *maxw, int *ok)
{
    /* devuelve el rango de ancho en que la consulta es verdadera */
    int lo_any = 65535, hi_any = 0, any_ok = 0;
    const char *p = s, *e = s + n;
    while (p <= e) {
        const char *q = p; int d = 0; while (q < e && !(*q == ',' && !d)) { if (*q == '(') d++; else if (*q == ')') d--; q++; }
        int lo = 0, hi = 65535, good = 1, neg = 0;
        const char *r = p;
        while (r < q) {
            while (r < q && wu_isspace(*r)) r++;
            if (r >= q) break;
            if (*r == '(') {
                const char *f = r + 1, *fe = f; int dd = 1; while (fe < q && dd) { if (*fe == '(') dd++; else if (*fe == ')') dd--; if (dd) fe++; }
                char b[64]; uint32_t fl = (uint32_t)(fe - f); if (fl >= sizeof(b)) fl = sizeof(b) - 1; memcpy(b, f, fl); b[fl] = 0;
                for (char *t = b; *t; t++) *t = (char)wu_lower(*t);
                const char *colon = b; while (*colon && *colon != ':') colon++;
                if (wu_startsi(b, "min-width") && *colon) { int v = wc_parse_length_px(colon + 1, 0); if (v > lo) lo = v; }
                else if (wu_startsi(b, "max-width") && *colon) { int v = wc_parse_length_px(colon + 1, 0); if (v < hi) hi = v; }
                else if (wu_startsi(b, "width") && !*colon) { const char *o = b + 5; while (wu_isspace(*o)) o++; int ge = (o[0] == '>'), le = (o[0] == '<'); if (ge || le) { o++; int eq = (*o == '='); if (eq) o++; int v = wc_parse_length_px(o, 0); if (ge) lo = v + (eq ? 0 : 1); else hi = v - (eq ? 0 : 1); } }
                else if (wu_find(b, "prefers-color-scheme") && wu_find(b, "dark")) good = 0;
                else if (wu_find(b, "prefers-reduced-motion") && wu_find(b, "reduce")) good = 0;
                else if (wu_find(b, "resolution") || wu_find(b, "pixel-ratio")) { if (wu_find(b, "min-") && !wu_find(b, "1")) good = 0; if (wu_find(b, "min-") && (wu_find(b, "1.5") || wu_find(b, "2") || wu_find(b, "192"))) good = 0; }
                else if (wu_find(b, "hover") && wu_find(b, "none")) good = 0;
                else if (wu_find(b, "pointer") && wu_find(b, "coarse")) good = 0;
                else if (wu_find(b, "orientation") && wu_find(b, "portrait") && c->vw > c->vh) good = 0;
                else if (wu_find(b, "min-aspect") || wu_find(b, "forced-colors") || wu_find(b, "prefers-contrast")) good = 0;
                r = fe < q ? fe + 1 : q; continue;
            }
            const char *w = r; while (r < q && !wu_isspace(*r) && *r != '(') r++;
            uint32_t wl = (uint32_t)(r - w);
            if (wl == 3 && wu_strnieq(w, "not", 3)) neg = 1;
            else if ((wl == 5 && wu_strnieq(w, "print", 5)) || (wl == 6 && wu_strnieq(w, "speech", 6))) good = 0;
        }
        if (neg) { if (lo == 0 && hi == 65535) good = !good; else good = 0; lo = 0; hi = 65535; }
        if (good) { any_ok = 1; if (lo < lo_any) lo_any = lo; if (hi > hi_any) hi_any = hi; }
        p = q + 1;
    }
    *ok = any_ok; *minw = any_ok ? lo_any : 0; *maxw = any_ok ? hi_any : 0;
}

static void add_import(struct wcss *c, const char *u, uint32_t n)
{
    if (c->nimp >= c->cimp) {
        int nc = c->cimp ? c->cimp * 2 : 8; char **ni = (char **)kmalloc(sizeof(char *) * (uint32_t)nc); if (!ni) return;
        if (c->imports) { memcpy(ni, c->imports, sizeof(char *) * (uint32_t)c->nimp); kfree(c->imports); }
        c->imports = ni; c->cimp = nc;
    }
    c->imports[c->nimp++] = wa_strndup(&c->d->ar, u, n);
}
const char *wc_next_import(struct wcss *c) { return c->imp_next < c->nimp ? c->imports[c->imp_next++] : 0; }

static void parse_rules(struct wcss *c, const char *s, const char *e, int origin, int minw, int maxw, int depth)
{
    const char *p = s;
    while (p < e) {
        while (p < e && (wu_isspace(*p) || (*p == '<' && p + 3 < e && p[1] == '!') )) { if (*p == '<') { while (p < e && *p != '>') p++; } p++; }
        if (p >= e) break;
        if (*p == '@') {
            const char *ns = ++p; while (p < e && (wu_isalpha(*p) || *p == '-')) p++;
            uint32_t nl = (uint32_t)(p - ns);
            const char *pre = p; int dd = 0;
            while (p < e && (dd || (*p != '{' && *p != ';'))) {
                if (*p == '(') dd++; else if (*p == ')') { if (dd) dd--; }
                else if (*p == '"' || *p == '\'') { char q = *p++; while (p < e && *p != q) p++; }
                p++;
            }
            const char *pre_e = p;
            if (p >= e) break;
            if (*p == ';') {
                p++;
                if (nl == 6 && wu_strnieq(ns, "import", 6) && depth == 0) {
                    const char *u = pre; while (u < pre_e && wu_isspace(*u)) u++;
                    const char *us; uint32_t ul = 0;
                    if (wu_startsi(u, "url(")) { u += 4; while (wu_isspace(*u)) u++; char q = (*u == '"' || *u == '\'') ? *u++ : 0; us = u; while (u < pre_e && (q ? *u != q : (*u != ')' && !wu_isspace(*u)))) u++; ul = (uint32_t)(u - us); while (u < pre_e && *u != ')') u++; if (u < pre_e) u++; }
                    else if (*u == '"' || *u == '\'') { char q = *u++; us = u; while (u < pre_e && *u != q) u++; ul = (uint32_t)(u - us); if (u < pre_e) u++; }
                    else continue;
                    int ok = 1, mn = 0, mx = 0; while (u < pre_e && wu_isspace(*u)) u++;
                    if (u < pre_e) media_eval(c, u, (uint32_t)(pre_e - u), &mn, &mx, &ok);
                    if (ok && ul) add_import(c, us, ul);
                }
                continue;
            }
            const char *bs = p + 1, *be = skip_to_block_end(bs, e);
            p = be < e ? be + 1 : e;
            if (depth > 4) continue;
            if (nl == 5 && wu_strnieq(ns, "media", 5)) {
                int mn, mx, ok; media_eval(c, pre, (uint32_t)(pre_e - pre), &mn, &mx, &ok);
                if (ok) parse_rules(c, bs, be, origin, mn > minw ? mn : minw, (mx && (!maxw || mx < maxw)) ? mx : maxw, depth + 1);
            } else if ((nl == 8 && wu_strnieq(ns, "supports", 8)) || (nl == 5 && wu_strnieq(ns, "layer", 5)) || (nl == 8 && wu_strnieq(ns, "document", 8))) {
                parse_rules(c, bs, be, origin, minw, maxw, depth + 1);
            }
            continue;
        }
        const char *ss = p; int qd = 0;
        while (p < e && (*p != '{' || qd)) { if (*p == '(' || *p == '[') qd++; else if (*p == ')' || *p == ']') { if (qd) qd--; } else if (*p == '"' || *p == '\'') { char q = *p++; while (p < e && *p != q) p++; } p++; }
        if (p >= e) break;
        const char *se = p; const char *bs = p + 1, *be = skip_to_block_end(bs, e);
        p = be < e ? be + 1 : e;
        /* declaraciones */
        int maxd = 1; for (const char *t = bs; t < be; t++) if (*t == ';') maxd++;
        struct wdecl *dl = (struct wdecl *)wa_alloc(&c->d->ar, (uint32_t)maxd * (uint32_t)sizeof(struct wdecl));
        if (!dl) return;
        int nd = parse_decls(c, bs, be, dl, maxd);
        if (!nd) continue;
        /* lista de selectores */
        const char *q = ss;
        while (q < se) {
            const char *r = q; int d2 = 0;
            while (r < se && !(*r == ',' && !d2)) { if (*r == '(' || *r == '[') d2++; else if (*r == ')' || *r == ']') d2--; r++; }
            struct wsel *sel = compile_selector(c, q, (uint32_t)(r - q));
            if (sel && sel->pe) { int hasclear = 0; for (int di = 0; di < nd; di++) if (dl[di].prop == PR_CLEAR && !is_kw(dl[di].val, dl[di].vlen, "none")) hasclear = 1; if (!hasclear) sel = 0; }
            if (sel) {
                struct wrule *ru = (struct wrule *)wa_zalloc(&c->d->ar, sizeof(*ru));
                if (!ru) return;
                ru->sel = sel; ru->decls = dl; ru->nd = (uint16_t)nd; ru->origin = (uint8_t)origin;
                ru->minw = (uint16_t)minw; ru->maxw = (uint16_t)(maxw ? maxw : 65535); ru->order = ++c->order;
                add_rule_ptr(c, ru);
            }
            q = r + 1;
        }
    }
}

void wc_add_sheet(struct wcss *c, const char *css, uint32_t len, int origin)
{
    if (!c || !css || !len) return;
    char *cp = (char *)wa_alloc(&c->d->ar, len + 1); if (!cp) return;
    uint32_t o = 0;
    for (uint32_t i = 0; i < len; i++) {            /* quita comentarios */
        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') { i += 2; while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/')) i++; i++; continue; }
        if (css[i] == '"' || css[i] == '\'') { char q = css[i]; cp[o++] = css[i++]; while (i < len && css[i] != q) { if (css[i] == '\\' && i + 1 < len) cp[o++] = css[i++]; cp[o++] = css[i++]; } if (i < len) cp[o++] = css[i]; continue; }
        cp[o++] = css[i];
    }
    cp[o] = 0;
    parse_rules(c, cp, cp + o, origin, 0, 0, 0);
}

/* ============================ calculo de estilos ============================ */
static void build_index(struct wcss *c)
{
    c->univ = 0; memset(c->tagt, 0, sizeof(c->tagt)); memset(c->idt, 0, sizeof(c->idt)); memset(c->clst, 0, sizeof(c->clst));
    for (int i = 0; i < c->nrules; i++) {
        struct wrule *r = c->rules[i]; const struct wsimple *s = &r->sel->parts[0];
        r->bnext = 0;
        struct wrule **slot;
        if (s->id) slot = &c->idt[wu_hash(s->id, wu_strlen(s->id)) & 127];
        else if (s->ncls) slot = &c->clst[wu_hash(s->cls[0], wu_strlen(s->cls[0])) & 127];
        else if (s->tag) slot = &c->tagt[s->tag];
        else slot = &c->univ;
        /* conserva el orden de aparicion */
        struct wrule **pp = slot; while (*pp) pp = &(*pp)->bnext; *pp = r;
    }
}

static const struct wvar *find_var(const struct wvar *v, const char *name, uint32_t nl)
{
    for (; v; v = v->next) if (wu_strneq(v->name, name, nl) && v->name[nl] == 0) return v;
    return 0;
}

static int expand_vars(struct wcss *c, const struct wvar *vars, const char *in, uint32_t n, char *out, uint32_t max, int depth)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; ) {
        if (n - i > 4 && wu_strnieq(in + i, "var(", 4)) {
            uint32_t j = i + 4, d = 1; while (j < n && wu_isspace(in[j])) j++;
            uint32_t ns = j; while (j < n && in[j] != ',' && in[j] != ')' && !wu_isspace(in[j])) j++;
            uint32_t nl = j - ns; while (j < n && wu_isspace(in[j])) j++;
            const char *fb = 0; uint32_t fl = 0;
            if (j < n && in[j] == ',') { j++; fb = in + j; uint32_t k = j; while (k < n) { if (in[k] == '(') d++; else if (in[k] == ')') { d--; if (!d) break; } k++; } fl = k - j; j = k; }
            while (j < n && d && in[j] != ')') j++;
            const struct wvar *v = find_var(vars, in + ns, nl);
            if (depth > 6) return 0;
            if (v) { int r = expand_vars(c, vars, v->val, wu_strlen(v->val), out + o, max - o, depth + 1); if (!r) return 0; o += (uint32_t)r - 1; }
            else if (fb) { int r = expand_vars(c, vars, fb, fl, out + o, max - o, depth + 1); if (!r) return 0; o += (uint32_t)r - 1; }
            else return 0;
            i = j + 1; continue;
        }
        if (o + 2 >= max) return 0;
        out[o++] = in[i++];
    }
    out[o] = 0; return (int)o + 1;
}

static void init_style(struct wstyle *st, const struct wstyle *par)
{
    memset(st, 0, sizeof(*st));
    st->display = D_INLINE; st->opacity = 255; st->flex_shrink = 100; st->width.kind = LK_AUTO; st->height.kind = LK_AUTO;
    st->min_w.kind = LK_VAL; st->min_h.kind = LK_VAL; st->max_w.kind = LK_NONE; st->max_h.kind = LK_NONE; st->flex_basis.kind = LK_AUTO;
    for (int i = 0; i < 4; i++) { st->margin[i].kind = LK_VAL; st->padding[i].kind = LK_VAL; st->border_color[i] = 1; }
    st->top.kind = st->right.kind = st->bottom.kind = st->left.kind = LK_AUTO;
    st->gap_r.kind = st->gap_c.kind = LK_VAL; st->text_indent.kind = LK_VAL; st->bg_w.kind = st->bg_h.kind = LK_AUTO;
    st->z_index = 0; st->spacing_h = st->spacing_v = 2; st->align_self = AI_AUTO;
    st->color = par->color; st->font_px = par->font_px; st->bold = par->bold; st->italic = par->italic; st->mono = par->mono;
    st->text_align = par->text_align; st->text_decor = par->text_decor; st->text_transform = par->text_transform; st->white_space = par->white_space;
    st->list_style = par->list_style; st->list_inside = par->list_inside; st->visibility = par->visibility; st->letter_sp = par->letter_sp;
    st->word_break = par->word_break; st->lh_mode = par->lh_mode; st->lh_val = par->lh_val; st->border_collapse = par->border_collapse;
    st->spacing_h = par->spacing_h; st->spacing_v = par->spacing_v; st->pointer = par->pointer; st->vars = par->vars;
    st->text_indent = par->text_indent; st->vertical_align = 0;
}

/* SOMA_PARCHE_028_UI: busqueda acotada a [h, h+hl). Los valores de las declaraciones apuntan DENTRO del texto de la hoja
 * (no terminan en nulo): buscar con wu_find recorria la hoja entera por cada declaracion y cada elemento (coste
 * cuadratico: en un periodico con 300 KB de CSS era la mayor parte del tiempo de carga). */
static const char *findn(const char *h, uint32_t hl, const char *n)
{
    uint32_t nl = wu_strlen(n); if (!nl || hl < nl) return 0;
    for (uint32_t i = 0; i + nl <= hl; i++) if (h[i] == n[0] && !memcmp(h + i, n, nl)) return h + i;
    return 0;
}
static void apply_decl(struct wcss *c, struct wstyle *st, struct actx *a, const struct wdecl *d)
{
    const char *v = d->val; uint32_t vl = d->vlen;
    if (d->prop == PR_CUSTOM) {
        struct wvar *nv = (struct wvar *)wa_alloc(&c->d->ar, sizeof(*nv)); if (!nv) return;
        nv->name = wa_strndup(&c->d->ar, d->name, d->nlen);
        uint32_t s = 0; while (s < vl && wu_isspace(v[s])) s++;
        nv->val = wa_strndup(&c->d->ar, v + s, vl - s); nv->next = st->vars; st->vars = nv; return;
    }
    if (vl > 4 && findn(v, vl, "var(")) {
        int r = expand_vars(c, st->vars, v, vl, c->scratch, 2048, 0);
        if (!r) return;
        v = c->scratch; vl = (uint32_t)r - 1;
    }
    apply_prop(a, st, d->prop, v, vl, 0, 0);
}

static void cascade_pass(struct wcss *c, struct wstyle *st, struct actx *a, struct wrule **m, int nm, const struct wdecl *inl, int ninl, int important)
{
    /* barrido 0: propiedades personalizadas, 1: fuente, 2: el resto */
    for (int sweep = 0; sweep < 3; sweep++) {
        for (int i = 0; i < nm + 1; i++) {
            const struct wdecl *dl; int nd;
            if (i < nm) { dl = m[i]->decls; nd = m[i]->nd; if (m[i]->origin == 0 && important) continue; } else { dl = inl; nd = ninl; }
            for (int k = 0; k < nd; k++) {
                const struct wdecl *d = &dl[k];
                if ((d->imp != 0) != (important != 0)) continue;
                int is_custom = d->prop == PR_CUSTOM;
                int is_font = d->prop == PR_FONT_SIZE || d->prop == PR_FONT;
                if ((sweep == 0) != is_custom) { if (sweep == 0 || is_custom) continue; }
                if (sweep == 1 && !is_font) continue;
                if (sweep == 2 && (is_font || is_custom)) continue;
                if (sweep == 0 && !is_custom) continue;
                a->lc.font_px = sweep == 1 ? a->par->font_px : st->font_px;
                if (sweep == 1) { apply_decl(c, st, a, d); }
                else { int save = a->lc.font_px; a->lc.font_px = st->font_px; apply_decl(c, st, a, d); a->lc.font_px = save; }
            }
        }
    }
}


/* atributos de presentacion HTML heredados (bgcolor, width, align, border, cellpadding, font...) */
static int attr_len(const char *v, struct wlen *o)
{
    if (!v) return 0;
    while (wu_isspace(*v)) v++;
    if (!wu_isdigit(*v)) return 0;
    int n = 0; while (wu_isdigit(*v)) { if (n < 100000) n = n * 10 + (*v - '0'); v++; }
    memset(o, 0, sizeof(*o)); o->kind = LK_VAL;
    if (*v == '%') o->pct = n * 100; else o->px = n;
    return 1;
}
static uint32_t attr_color(const char *v)
{
    int ok; uint32_t c = wc_parse_color(v, &ok);
    if (ok) return c;
    char b[16]; uint32_t l = wu_strlen(v); if (l > 6 || l == 0) return 0;
    b[0] = '#'; memcpy(b + 1, v, l); b[l + 1] = 0;
    c = wc_parse_color(b, &ok); return ok ? c : 0;
}
static void apply_presentational(struct wnode *n, struct wstyle *st)
{
    if (!n->attrs) return;
    const char *v; struct wlen l; int t = n->tag;
    if (wd_attr(n, "hidden") && t != WT_INPUT) { st->display = D_NONE; return; }
    if ((v = wd_attr(n, "bgcolor")) && !st->bg_color) { uint32_t c = attr_color(v); if (c) st->bg_color = c; }
    if (t == WT_BODY) { if ((v = wd_attr(n, "text"))) { uint32_t c = attr_color(v); if (c) st->color = c; } }
    if (t == WT_TABLE || t == WT_TD || t == WT_TH || t == WT_IMG || t == WT_IFRAME || t == WT_CANVAS || t == WT_VIDEO || t == WT_SVG || t == WT_EMBED || t == WT_OBJECT || t == WT_HR || t == WT_COL || t == WT_COLGROUP || t == WT_INPUT) {
        /* SOMA_PARCHE_028_UI: en imagenes y videos, si el autor fija solo una dimension por CSS (width:100%; height:auto),
         * el atributo de la otra no se aplica: queda como proporcion (lo usa la maquetacion), igual que en los navegadores actuales */
        int media = (t == WT_IMG || t == WT_VIDEO || t == WT_CANVAS || t == WT_SVG);
        int css_w = st->width.kind != LK_AUTO, css_h = st->height.kind != LK_AUTO;
        if (st->width.kind == LK_AUTO && !(media && css_h) && (v = wd_attr(n, "width")) && attr_len(v, &l)) st->width = l;
        if (st->height.kind == LK_AUTO && !(media && css_w) && (v = wd_attr(n, "height")) && attr_len(v, &l)) st->height = l;
    }
    if ((v = wd_attr(n, "align"))) {
        int a = wu_strieq(v, "center") ? 1 : (wu_strieq(v, "right") ? 2 : (wu_strieq(v, "left") ? 3 : 0));
        if (t == WT_TABLE) { if (a == 1) { st->margin[1].kind = st->margin[3].kind = LK_AUTO; } else if (a == 2) st->floating = FL_RIGHT; else if (a == 3) st->floating = FL_LEFT; }
        else if (t == WT_IMG) { if (a == 2) st->floating = FL_RIGHT; else if (a == 3) st->floating = FL_LEFT; }
        else if (t == WT_DIV || t == WT_P || (t >= WT_H1 && t <= WT_H6) || t == WT_TD || t == WT_TH || t == WT_TR || t == WT_TBODY || t == WT_CAPTION || t == WT_CENTER || t == WT_THEAD || t == WT_TFOOT || t == WT_LEGEND) {
            if (a == 1) st->text_align = TA_CENTER; else if (a == 2) st->text_align = TA_RIGHT; else if (a == 3) st->text_align = TA_LEFT; else if (wu_strieq(v, "justify")) st->text_align = TA_JUSTIFY;
        }
    }
    if ((t == WT_TD || t == WT_TH || t == WT_TR || t == WT_TBODY) && (v = wd_attr(n, "valign"))) {
        st->vertical_align = wu_strieq(v, "top") ? VA_TOP : (wu_strieq(v, "bottom") ? VA_BOTTOM : (wu_strieq(v, "middle") ? VA_MIDDLE : st->vertical_align));
    }
    if (t == WT_TD || t == WT_TH) {
        if (wd_attr(n, "nowrap")) st->white_space = WS_NOWRAP;
        struct wnode *tb = n->parent; int guard = 0;
        while (tb && tb->tag != WT_TABLE && guard++ < 4) tb = tb->parent;
        if (tb && tb->tag == WT_TABLE) {
            int b = 0; if ((v = wd_attr(tb, "border"))) b = *v ? wu_atoi(v) : 1;
            if (b > 0) for (int i = 0; i < 4; i++) if (!st->border_style[i] && !st->border_w[i]) { st->border_style[i] = 1; st->border_w[i] = 1; st->border_color[i] = 0xFF808080u; }
            if ((v = wd_attr(tb, "cellpadding")) && attr_len(v, &l)) {
                int all0 = 1; for (int i = 0; i < 4; i++) if (st->padding[i].px || st->padding[i].pct) all0 = 0;
                if (all0) for (int i = 0; i < 4; i++) st->padding[i] = l;
            }
        }
    }
    if (t == WT_TABLE) {
        if ((v = wd_attr(n, "border")) ) { int b = *v ? wu_atoi(v) : 1; if (b > 0) for (int i = 0; i < 4; i++) if (!st->border_style[i] && !st->border_w[i]) { st->border_style[i] = 1; st->border_w[i] = (int16_t)b; st->border_color[i] = 0xFF808080u; } }
        if ((v = wd_attr(n, "cellspacing")) && attr_len(v, &l)) { st->spacing_h = st->spacing_v = (int16_t)l.px; }
    }
    if (t == WT_FONT) {
        if ((v = wd_attr(n, "color"))) { uint32_t c = attr_color(v); if (c) st->color = c; }
        if ((v = wd_attr(n, "size"))) {
            static const int fs[8] = { 13, 10, 13, 16, 18, 24, 32, 48 };
            int sz = wu_atoi(v); int rel = (v[0] == '+' || v[0] == '-');
            if (rel) sz = 3 + sz;
            if (sz < 1) sz = 1;
            if (sz > 7) sz = 7;
            st->font_px = (int16_t)fs[sz];
        }
        if ((v = wd_attr(n, "face"))) st->mono = (uint8_t)family_is_mono(v, wu_strlen(v));
    }
    if (t == WT_UL || t == WT_OL || t == WT_LI) {
        if ((v = wd_attr(n, "type"))) {
            if (v[0] == '1') st->list_style = LS_DECIMAL; else if (v[0] == 'a') st->list_style = LS_ALPHA; else if (v[0] == 'A') st->list_style = LS_UALPHA;
            else if (v[0] == 'i' || v[0] == 'I') st->list_style = LS_ROMAN; else if (wu_strieq(v, "disc")) st->list_style = LS_DISC; else if (wu_strieq(v, "circle")) st->list_style = LS_CIRCLE; else if (wu_strieq(v, "square")) st->list_style = LS_SQUARE;
        }
    }
    if (t == WT_A && wd_attr(n, "href") && n->parent && n->parent->tag == WT_BODY) { /* link=... del body */ const char *lc = wd_attr(n->parent, "link"); if (lc) { uint32_t c = attr_color(lc); if (c && st->color == 0xFF0000EEu) st->color = c; } }
}

static void compute_node(struct wcss *c, struct wnode *n, const struct wstyle *par, struct wdecl *tmpd)
{
    wu_tick();
    struct wstyle *st = n->st;
    if (!st) { st = (struct wstyle *)wa_alloc(&c->d->ar, sizeof(*st)); if (!st) return; n->st = st; }
    init_style(st, par);
    /* reglas coincidentes */
    int nm = 0; struct wrule **m = c->match;
    struct wrule *lists[8]; int nl = 0;
    lists[nl++] = c->univ;
    if (n->tag) lists[nl++] = c->tagt[n->tag];
    const char *id = wd_attr(n, "id");
    if (id && *id) lists[nl++] = c->idt[wu_hash(id, wu_strlen(id)) & 127];
    const char *cl = wd_attr(n, "class");
    struct wrule *clists[16]; int ncl = 0;
    if (cl) { const char *p = cl; while (*p && ncl < 16) { while (wu_isspace(*p)) p++; const char *s = p; while (*p && !wu_isspace(*p)) p++; if (p > s) clists[ncl++] = c->clst[wu_hash(s, (uint32_t)(p - s)) & 127]; } }
    for (int li = 0; li < nl + ncl; li++) {
        struct wrule *r = li < nl ? lists[li] : clists[li - nl];
        for (; r; r = r->bnext) {
            if (c->vw < r->minw || c->vw > r->maxw) continue;
            if (!match_from(r->sel, 0, n)) continue;
            if (r->sel->pe) { st->clearfix = 1; continue; }
            if (nm < c->match_cap) m[nm++] = r;
        }
    }
    /* orden: origen, especificidad, aparicion (insercion; listas casi ordenadas) */
    for (int i = 1; i < nm; i++) {
        struct wrule *x = m[i]; int j = i - 1;
        uint64_t kx = ((uint64_t)x->origin << 56) | ((uint64_t)x->sel->spec << 24) | (x->order & 0xFFFFFF);
        while (j >= 0) { struct wrule *y = m[j]; uint64_t ky = ((uint64_t)y->origin << 56) | ((uint64_t)y->sel->spec << 24) | (y->order & 0xFFFFFF); if (ky <= kx) break; m[j + 1] = y; j--; }
        m[j + 1] = x;
    }
    int ninl = 0; const char *sty = wd_attr(n, "style");
    if (sty && *sty) { uint32_t sl = wu_strlen(sty); ninl = parse_decls(c, sty, sty + sl, tmpd, 96); }
    struct actx a; a.par = par; a.c = c; a.lc.root_px = 16; a.lc.vw = c->vw; a.lc.vh = c->vh; a.lc.font_px = par->font_px;
    cascade_pass(c, st, &a, m, nm, tmpd, ninl, 0);
    cascade_pass(c, st, &a, m, nm, tmpd, ninl, 1);
    for (int i = 0; i < 4; i++) if (st->border_color[i] == 1) st->border_color[i] = st->color;
    apply_presentational(n, st);
    /* flotantes y posicion absoluta bloquean el display */
    if ((st->floating || st->position == P_ABSOLUTE || st->position == P_FIXED) && st->display != D_NONE) {
        if (st->display == D_INLINE || st->display == D_INLINE_BLOCK) st->display = D_BLOCK;
        else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
        else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    }
    n->flags &= (uint16_t)~WNF_DIRTY;
}

void wc_compute(struct wcss *c, int vw, int vh)
{
    if (!c) return;
    c->vw = vw; c->vh = vh;
    build_index(c);
    struct wstyle *root = (struct wstyle *)kmalloc(sizeof(struct wstyle));
    struct wdecl *tmpd = (struct wdecl *)kmalloc(sizeof(struct wdecl) * 96);
    if (!root || !tmpd) { if (root) kfree(root); if (tmpd) kfree(tmpd); return; }
    memset(root, 0, sizeof(*root)); root->font_px = 16; root->color = 0xFF000000u; root->spacing_h = root->spacing_v = 2;
    struct wnode *docn = c->d->root;
    for (struct wnode *n = docn->first; n; ) {
        if (n->type == WN_ELEM) {
            const struct wstyle *par = (n->parent && n->parent->st) ? n->parent->st : root;
            compute_node(c, n, par, tmpd);
        }
        if (n->type == WN_ELEM && n->first) { n = n->first; continue; }
        while (n && !n->next) { n = n->parent; if (n == docn) { n = 0; break; } }
        if (n) n = n->next;
    }
    kfree(root); kfree(tmpd);
}

/* ============================ consultas ============================ */
static struct wsel *cached_sel(struct wcss *c, const char *sel, uint32_t n)
{
    return compile_selector(c, sel, n);
}

static int query_one(struct wcss *c, struct wnode *root, const char *sel, struct wnode **out, int max, int first_only, int *count)
{
    const char *p = sel, *e = sel + wu_strlen(sel);
    struct wsel *list[8]; int nl = 0;
    while (p < e && nl < 8) {
        const char *r = p; int d = 0; while (r < e && !(*r == ',' && !d)) { if (*r == '(' || *r == '[') d++; else if (*r == ')' || *r == ']') d--; r++; }
        struct wsel *s = cached_sel(c, p, (uint32_t)(r - p));
        if (!s) return 0;
        list[nl++] = s; p = r + 1;
    }
    for (struct wnode *n = root->first; n; ) {
        if (n->type == WN_ELEM) {
            for (int i = 0; i < nl; i++) if (match_from(list[i], 0, n)) { if (*count < max && out) out[*count] = n; (*count)++; if (first_only) return 1; break; }
        }
        if (n->type == WN_ELEM && n->first) { n = n->first; continue; }
        while (n && n != root && !n->next) n = n->parent;
        if (!n || n == root) break;
        n = n->next;
    }
    return 1;
}

int wc_query(struct wcss *c, struct wnode *root, const char *sel, struct wnode **out, int max, int first_only)
{
    int count = 0;
    if (!c || !root || !sel) return 0;
    size_t mark = 0; (void)mark;
    query_one(c, root, sel, out, max, first_only, &count);
    return count > max ? max : count;
}

int wc_matches(struct wcss *c, struct wnode *n, const char *sel)
{
    const char *p = sel, *e = sel + wu_strlen(sel);
    while (p < e) {
        const char *r = p; int d = 0; while (r < e && !(*r == ',' && !d)) { if (*r == '(' || *r == '[') d++; else if (*r == ')' || *r == ']') d--; r++; }
        struct wsel *s = cached_sel(c, p, (uint32_t)(r - p));
        if (s && match_from(s, 0, n)) return 1;
        p = r + 1;
    }
    return 0;
}

/* ============================ estilo en linea ============================ */
void wc_set_inline(struct wdoc *d, struct wnode *n, const char *prop, const char *val)
{
    struct wb out = {0, 0, 0, 0}; const char *cur = wd_attr(n, "style"); int done = 0;
    uint32_t pl = wu_strlen(prop);
    if (cur) {
        const char *p = cur;
        while (*p) {
            while (*p == ';' || wu_isspace(*p)) p++;
            if (!*p) break;
            const char *ns = p; int dp = 0; while (*p && (*p != ';' || dp)) { if (*p == '(') dp++; else if (*p == ')' && dp) dp--; p++; }
            const char *ne = ns; while (ne < p && *ne != ':') ne++;
            const char *nt = ne; while (nt > ns && wu_isspace(nt[-1])) nt--;
            if ((uint32_t)(nt - ns) == pl && wu_strnieq(ns, prop, pl)) {
                if (val && *val && !done) { wb_add(&out, prop); wb_add(&out, ": "); wb_add(&out, val); wb_add(&out, "; "); }
                done = 1;
            } else { wb_addn(&out, ns, (uint32_t)(p - ns)); wb_add(&out, "; "); }
        }
    }
    if (!done && val && *val) { wb_add(&out, prop); wb_add(&out, ": "); wb_add(&out, val); wb_add(&out, ";"); }
    wd_set_attr(d, n, "style", out.p ? out.p : "");
    wb_free(&out);
}

const char *wc_get_inline(struct wnode *n, const char *prop, char *out, uint32_t max)
{
    const char *cur = wd_attr(n, "style"); uint32_t pl = wu_strlen(prop);
    out[0] = 0;
    if (!cur) return out;
    const char *p = cur;
    while (*p) {
        while (*p == ';' || wu_isspace(*p)) p++;
        if (!*p) break;
        const char *ns = p; while (*p && *p != ';') p++;
        const char *ne = ns; while (ne < p && *ne != ':') ne++;
        const char *nt = ne; while (nt > ns && wu_isspace(nt[-1])) nt--;
        if ((uint32_t)(nt - ns) == pl && wu_strnieq(ns, prop, pl) && ne < p) {
            const char *vs = ne + 1; while (vs < p && wu_isspace(*vs)) vs++;
            const char *ve = p; while (ve > vs && wu_isspace(ve[-1])) ve--;
            wu_copyn(out, vs, (uint32_t)(ve - vs), max); return out;
        }
    }
    return out;
}
