/* SOMA_PARCHE_NAVEGADOR: motor de expresiones regulares (retroceso, UTF-8). */
#include "web_regex.h"

const char *wre_stack_lo;

enum { R_EMPTY = 1, R_CHAR, R_ANY, R_CLASS, R_BOL, R_EOL, R_WORDB, R_NWORDB, R_BACK, R_NBACK, R_GROUP, R_REPEAT, R_LOOK };
struct rng { uint32_t lo, hi; };
struct rnode {
    uint8_t t, neg, greedy, behind, simple;
    int32_t a, min, max, cap_lo, cap_hi;
    struct rnode *next, *altnext, *alts;          /* alts: lista de secuencias (GROUP/LOOK) o cuerpo (REPEAT) */
    struct rng *rs; int nrs; char *name;
};
struct wre { struct rnode top; int ngroups, flags; char *names[WRE_MAXG + 1]; int first; };

/* ------------------------------------------------------------------ analisis */
struct rp { struct wre *re; struct wa_arena *ar; const char *e, *cur; char *err; uint32_t errmax; int failed, depth, uni; };

static void fail(struct rp *p, const char *m) { if (!p->failed) wu_copy(p->err, m, p->errmax); p->failed = 1; }
static struct rnode g_dummy;
static struct rnode *nn(struct rp *p, int t)
{
    struct rnode *n = (struct rnode *)wa_zalloc(p->ar, (uint32_t)sizeof *n);
    if (!n) { fail(p, "memoria agotada"); memset(&g_dummy, 0, sizeof g_dummy); g_dummy.t = R_EMPTY; return &g_dummy; }
    n->t = (uint8_t)t; n->greedy = 1; n->max = -1; return n;
}
static uint32_t fold(uint32_t c)
{
    if (c < 128) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    if (c >= 0x391 && c <= 0x3A9) return c + 32;
    if (c >= 0x100 && c <= 0x137 && !(c & 1)) return c + 1;
    return c;
}
static uint32_t upper(uint32_t c)
{
    if (c < 128) return (c >= 'a' && c <= 'z') ? c - 32 : c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x101 && c <= 0x137 && (c & 1)) return c - 1;
    return c;
}
static int is_word(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

static void add_rng(struct rp *p, struct rnode *n, uint32_t lo, uint32_t hi)
{
    struct rng *nr = (struct rng *)wa_alloc(p->ar, (uint32_t)(n->nrs + 1) * (uint32_t)sizeof(struct rng));
    if (!nr) { fail(p, "memoria agotada"); return; }
    if (n->nrs) memcpy(nr, n->rs, (uint32_t)n->nrs * sizeof(struct rng));
    nr[n->nrs].lo = lo; nr[n->nrs].hi = hi; n->rs = nr; n->nrs++;
}
static void class_esc(struct rp *p, struct rnode *n, char c)
{
    switch (c) {
    case 'd': add_rng(p, n, '0', '9'); break;
    case 'w': add_rng(p, n, '0', '9'); add_rng(p, n, 'A', 'Z'); add_rng(p, n, '_', '_'); add_rng(p, n, 'a', 'z'); break;
    case 's': add_rng(p, n, 9, 13); add_rng(p, n, ' ', ' '); add_rng(p, n, 0xA0, 0xA0); add_rng(p, n, 0x1680, 0x1680); add_rng(p, n, 0x2000, 0x200A);
              add_rng(p, n, 0x2028, 0x2029); add_rng(p, n, 0x202F, 0x202F); add_rng(p, n, 0x205F, 0x205F); add_rng(p, n, 0x3000, 0x3000); add_rng(p, n, 0xFEFF, 0xFEFF); break;
    case 'D': add_rng(p, n, 0, '0' - 1); add_rng(p, n, '9' + 1, 0x10FFFF); break;
    case 'W': add_rng(p, n, 0, '0' - 1); add_rng(p, n, '9' + 1, 'A' - 1); add_rng(p, n, 'Z' + 1, '_' - 1); add_rng(p, n, '_' + 1, 'a' - 1); add_rng(p, n, 'z' + 1, 0x10FFFF); break;
    case 'S': add_rng(p, n, 0, 8); add_rng(p, n, 14, 31); add_rng(p, n, 33, 0x9F); add_rng(p, n, 0xA1, 0x167F); add_rng(p, n, 0x1681, 0x1FFF); add_rng(p, n, 0x200B, 0x2027);
              add_rng(p, n, 0x202A, 0x202E); add_rng(p, n, 0x2030, 0x205E); add_rng(p, n, 0x2060, 0x2FFF); add_rng(p, n, 0x3001, 0xFEFE); add_rng(p, n, 0xFF00, 0x10FFFF); break;
    default: break;
    }
}
/* propiedades Unicode (aproximadas) para \p{..} */
#define RS(...) (const struct rng[]){ __VA_ARGS__ }
struct prop { const char *name; const struct rng *r; int n; };
static const struct rng P_L[] = { {'A','Z'},{'a','z'},{0xAA,0xAA},{0xB5,0xB5},{0xBA,0xBA},{0xC0,0xD6},{0xD8,0xF6},{0xF8,0x2C1},{0x370,0x373},{0x376,0x377},{0x37B,0x37D},{0x386,0x386},{0x388,0x481},
    {0x48A,0x52F},{0x531,0x556},{0x561,0x587},{0x5D0,0x5EA},{0x620,0x64A},{0x671,0x6D3},{0x904,0x939},{0xE01,0xE30},{0x10A0,0x10FF},{0x1E00,0x1FFF},{0x3041,0x3096},{0x30A1,0x30FA},
    {0x3400,0x4DBF},{0x4E00,0x9FFF},{0xAC00,0xD7A3},{0xF900,0xFAFF},{0xFF21,0xFF3A},{0xFF41,0xFF5A},{0x20000,0x2FA1F} };
static const struct rng P_LU[] = { {'A','Z'},{0xC0,0xD6},{0xD8,0xDE},{0x391,0x3A9},{0x400,0x42F} };
static const struct rng P_LL[] = { {'a','z'},{0xDF,0xF6},{0xF8,0xFF},{0x3B1,0x3C9},{0x430,0x45F} };
static const struct rng P_ND[] = { {'0','9'},{0x660,0x669},{0x6F0,0x6F9},{0x966,0x96F},{0xFF10,0xFF19} };
static const struct rng P_N[] = { {'0','9'},{0xB2,0xB3},{0xB9,0xB9},{0xBC,0xBE},{0x660,0x669},{0x6F0,0x6F9},{0x966,0x96F},{0x2150,0x218F},{0xFF10,0xFF19} };
static const struct rng P_P[] = { {0x21,0x23},{0x25,0x2A},{0x2C,0x2F},{0x3A,0x3B},{0x3F,0x40},{0x5B,0x5D},{0x5F,0x5F},{0x7B,0x7B},{0x7D,0x7D},{0xA1,0xA1},{0xA7,0xA7},{0xAB,0xAB},{0xB6,0xB7},
    {0xBB,0xBB},{0xBF,0xBF},{0x2010,0x2027},{0x2030,0x205E},{0x3001,0x3003},{0x3008,0x3011} };
static const struct rng P_S[] = { {0x24,0x24},{0x2B,0x2B},{0x3C,0x3E},{0x5E,0x5E},{0x60,0x60},{0x7C,0x7C},{0x7E,0x7E},{0xA2,0xA6},{0xA8,0xA9},{0xAC,0xAC},{0xAE,0xB1},{0xB4,0xB4},{0xB8,0xB8},
    {0xD7,0xD7},{0xF7,0xF7},{0x20A0,0x20CF},{0x2190,0x23FF},{0x2600,0x27BF},{0x1F300,0x1FAFF} };
static const struct rng P_Z[] = { {0x20,0x20},{0xA0,0xA0},{0x1680,0x1680},{0x2000,0x200A},{0x2028,0x2029},{0x202F,0x202F},{0x205F,0x205F},{0x3000,0x3000} };
static const struct rng P_EMOJI[] = { {0xA9,0xA9},{0xAE,0xAE},{0x203C,0x203C},{0x2049,0x2049},{0x2122,0x2122},{0x2139,0x2139},{0x2194,0x21AA},{0x231A,0x23FF},{0x24C2,0x24C2},{0x25AA,0x25FE},
    {0x2600,0x27BF},{0x2934,0x2935},{0x2B05,0x2B55},{0x3030,0x3030},{0x303D,0x303D},{0x3297,0x3299},{0x1F000,0x1FAFF} };
#define PN(a) a, (int)(sizeof(a) / sizeof(a[0]))
static const struct prop PROPS[] = {
    { "L", PN(P_L) }, { "Letter", PN(P_L) }, { "Alphabetic", PN(P_L) }, { "Alpha", PN(P_L) }, { "Lu", PN(P_LU) }, { "Uppercase_Letter", PN(P_LU) }, { "Uppercase", PN(P_LU) },
    { "Ll", PN(P_LL) }, { "Lowercase_Letter", PN(P_LL) }, { "Lowercase", PN(P_LL) }, { "Nd", PN(P_ND) }, { "Decimal_Number", PN(P_ND) }, { "N", PN(P_N) }, { "Number", PN(P_N) },
    { "P", PN(P_P) }, { "Punctuation", PN(P_P) }, { "S", PN(P_S) }, { "Symbol", PN(P_S) }, { "Z", PN(P_Z) }, { "Zs", PN(P_Z) }, { "Separator", PN(P_Z) }, { "White_Space", PN(P_Z) },
    { "Emoji", PN(P_EMOJI) }, { "Extended_Pictographic", PN(P_EMOJI) }, { "Emoji_Presentation", PN(P_EMOJI) },
};
static void add_prop(struct rp *p, struct rnode *n, const char *nm, uint32_t nl, int neg)
{
    const struct prop *pr = &PROPS[0];                          /* desconocida -> letras */
    char key[40]; uint32_t kl = nl;
    for (uint32_t i = 0; i < nl && i < sizeof key - 1; i++) key[i] = nm[i];
    if (nl >= sizeof key) kl = sizeof key - 1;
    key[kl] = 0;
    const char *eq = wu_strchr(key, '=');
    if (!eq || !(key[0] == 'S' || key[0] == 's')) { const char *k2 = eq ? eq + 1 : key; for (uint32_t i = 0; i < sizeof PROPS / sizeof PROPS[0]; i++) if (wu_streq(PROPS[i].name, k2)) { pr = &PROPS[i]; break; } }
    if (!neg) { for (int i = 0; i < pr->n; i++) add_rng(p, n, pr->r[i].lo, pr->r[i].hi); return; }
    uint32_t next = 0;
    for (int i = 0; i < pr->n; i++) { if (pr->r[i].lo > next) add_rng(p, n, next, pr->r[i].lo - 1); next = pr->r[i].hi + 1; }
    add_rng(p, n, next, 0x10FFFF);
}
static uint32_t read_cp(struct rp *p)
{
    uint32_t adv = 1, c = wu_utf8_next(p->cur, &adv);
    if (!adv) adv = 1;
    p->cur += adv; return c;
}
/* tras '\\'. Devuelve el codepoint, o 0 con *cls = d,D,w,W,s,S,p,P si es una clase */
static uint32_t read_escape(struct rp *p, int in_class, char *cls, struct rnode *cnode)
{
    char c = *p->cur++; *cls = 0;
    switch (c) {
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S': *cls = c; return 0;
    case 'p': case 'P':
        if (p->uni && p->cur < p->e && *p->cur == '{') {
            const char *a = ++p->cur; while (p->cur < p->e && *p->cur != '}') p->cur++;
            if (p->cur >= p->e) { fail(p, "propiedad unicode sin cerrar"); return 0; }
            add_prop(p, cnode, a, (uint32_t)(p->cur - a), c == 'P'); p->cur++; *cls = 'p'; return 0;
        }
        return (uint32_t)(uint8_t)c;
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case 'f': return '\f'; case 'v': return '\v'; case '0': return 0;
    case 'b': if (in_class) return '\b'; return 'b';
    case 'x': if (p->cur + 2 <= p->e && wu_ishex(p->cur[0]) && wu_ishex(p->cur[1])) { uint32_t v = (uint32_t)(wu_hexval(p->cur[0]) * 16 + wu_hexval(p->cur[1])); p->cur += 2; return v; } return 'x';
    case 'u':
        if (p->cur < p->e && *p->cur == '{' && p->uni) {
            p->cur++; uint32_t v = 0;
            while (p->cur < p->e && *p->cur != '}') { if (!wu_ishex(*p->cur)) { fail(p, "escape unicode invalido"); return 0; } v = v * 16 + (uint32_t)wu_hexval(*p->cur++); if (v > 0x10FFFF) { fail(p, "codepoint fuera de rango"); return 0; } }
            p->cur++; return v;
        }
        if (p->cur + 4 <= p->e && wu_ishex(p->cur[0]) && wu_ishex(p->cur[1]) && wu_ishex(p->cur[2]) && wu_ishex(p->cur[3])) {
            uint32_t v = 0; for (int i = 0; i < 4; i++) v = v * 16 + (uint32_t)wu_hexval(*p->cur++);
            if (v >= 0xD800 && v <= 0xDBFF && p->cur + 6 <= p->e && p->cur[0] == '\\' && p->cur[1] == 'u' && wu_ishex(p->cur[2]) && wu_ishex(p->cur[3]) && wu_ishex(p->cur[4]) && wu_ishex(p->cur[5])) {
                uint32_t lo = 0; for (int i = 2; i < 6; i++) lo = lo * 16 + (uint32_t)wu_hexval(p->cur[i]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) { p->cur += 6; return 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00); }
            }
            return v >= 0xD800 && v <= 0xDFFF ? 0xFFFD : v;
        }
        return 'u';
    case 'c': if (p->cur < p->e && wu_isalpha(*p->cur)) return (uint32_t)(*p->cur++ & 31); return 'c';
    default: break;
    }
    if ((uint8_t)c >= 0x80) { p->cur--; return read_cp(p); }
    return (uint32_t)(uint8_t)c;
}
static struct rnode *parse_class(struct rp *p)
{
    struct rnode *n = nn(p, R_CLASS);
    if (p->cur < p->e && *p->cur == '^') { n->neg = 1; p->cur++; }
    while (p->cur < p->e && *p->cur != ']' && !p->failed) {
        uint32_t lo; char cls = 0;
        if (*p->cur == '\\') { p->cur++; if (p->cur >= p->e) { fail(p, "escape al final"); break; } lo = read_escape(p, 1, &cls, n); if (cls == 'p') continue; if (cls) { class_esc(p, n, cls); continue; } }
        else lo = read_cp(p);
        if (p->cur + 1 < p->e && *p->cur == '-' && p->cur[1] != ']') {
            p->cur++; uint32_t hi; char c2 = 0;
            if (*p->cur == '\\') { p->cur++; hi = read_escape(p, 1, &c2, n); if (c2) { add_rng(p, n, lo, lo); add_rng(p, n, '-', '-'); if (c2 != 'p') class_esc(p, n, c2); continue; } }
            else hi = read_cp(p);
            if (hi < lo) { fail(p, "rango invalido en la clase"); break; }
            add_rng(p, n, lo, hi);
        } else add_rng(p, n, lo, lo);
    }
    if (p->cur >= p->e) fail(p, "clase sin cerrar"); else p->cur++;
    return n;
}
static int quant_at(struct rp *p, int32_t *mn, int32_t *mx, const char **after)
{
    /* cur apunta a '{'; devuelve 1 si es {n}, {n,} o {n,m} valido */
    const char *c = p->cur + 1; int64_t a = 0, b = -1; int any = 0;
    while (c < p->e && wu_isdigit(*c)) { if (a < 100000) a = a * 10 + (*c - '0'); c++; any = 1; }
    if (!any) return 0;
    if (c < p->e && *c == ',') {
        c++; int any2 = 0; int64_t v = 0;
        while (c < p->e && wu_isdigit(*c)) { if (v < 100000) v = v * 10 + (*c - '0'); c++; any2 = 1; }
        b = any2 ? v : -1;
    } else b = a;
    if (c >= p->e || *c != '}') return 0;
    *mn = (int32_t)a; *mx = (int32_t)b; *after = c + 1; return 1;
}
static struct rnode *parse_disj(struct rp *p, struct rnode *holder);
/* (?:a|[b-c]|d) -> una sola clase: permite el camino rapido de repeticion simple */
static struct rnode *try_merge(struct rp *p, struct rnode *g)
{
    if (g->t != R_GROUP || g->a >= 0 || p->failed) return g;
    for (struct rnode *s = g->alts; s; s = s->altnext)
        if (s->next || !(s->t == R_CHAR || (s->t == R_CLASS && !s->neg))) return g;
    struct rnode *c = nn(p, R_CLASS);
    for (struct rnode *s = g->alts; s; s = s->altnext) {
        if (s->t == R_CHAR) add_rng(p, c, (uint32_t)s->a, (uint32_t)s->a);
        else for (int i = 0; i < s->nrs; i++) add_rng(p, c, s->rs[i].lo, s->rs[i].hi);
    }
    return c;
}
static struct rnode *parse_atom(struct rp *p)
{
    char c = *p->cur;
    if (c == '^') { p->cur++; return nn(p, R_BOL); }
    if (c == '$') { p->cur++; return nn(p, R_EOL); }
    if (c == '.') { p->cur++; return nn(p, R_ANY); }
    if (c == '[') { p->cur++; return parse_class(p); }
    if (c == '*' || c == '+' || c == '?') { fail(p, "nada que repetir"); p->cur++; return nn(p, R_EMPTY); }
    if (c == '{') { int32_t a, b; const char *af; if (quant_at(p, &a, &b, &af)) { fail(p, "nada que repetir"); return nn(p, R_EMPTY); } p->cur++; struct rnode *n = nn(p, R_CHAR); n->a = '{'; return n; }
    if (c == '(') {
        p->cur++; int kind = 0; const char *name = 0; uint32_t nlen = 0;   /* 0 captura, 1 no captura, 2 =, 3 !, 4 <=, 5 <! */
        if (p->cur < p->e && *p->cur == '?') {
            if (p->cur + 1 < p->e && p->cur[1] == ':') { kind = 1; p->cur += 2; }
            else if (p->cur + 1 < p->e && p->cur[1] == '=') { kind = 2; p->cur += 2; }
            else if (p->cur + 1 < p->e && p->cur[1] == '!') { kind = 3; p->cur += 2; }
            else if (p->cur + 2 < p->e && p->cur[1] == '<' && (p->cur[2] == '=' || p->cur[2] == '!')) { kind = p->cur[2] == '=' ? 4 : 5; p->cur += 3; }
            else if (p->cur + 1 < p->e && p->cur[1] == '<') { p->cur += 2; name = p->cur; while (p->cur < p->e && *p->cur != '>') p->cur++; nlen = (uint32_t)(p->cur - name); if (p->cur < p->e) p->cur++; }
            else { fail(p, "grupo no valido"); return nn(p, R_EMPTY); }
        }
        struct rnode *g = nn(p, kind >= 2 ? R_LOOK : R_GROUP); g->a = -1;
        if (kind == 0) {
            if (p->re->ngroups >= WRE_MAXG) { fail(p, "demasiados grupos de captura"); return g; }
            g->a = ++p->re->ngroups; if (name) p->re->names[g->a] = wa_strndup(p->ar, name, nlen);
        } else if (kind >= 2) { g->neg = (kind == 3 || kind == 5); g->behind = (kind == 4 || kind == 5); }
        if (++p->depth > 100) { fail(p, "expresion demasiado anidada"); return g; }
        parse_disj(p, g); p->depth--;
        if (p->cur >= p->e || *p->cur != ')') { fail(p, "falta ')'"); return g; }
        p->cur++; return try_merge(p, g);
    }
    if (c == '\\') {
        p->cur++;
        if (p->cur >= p->e) { fail(p, "escape al final del patron"); return nn(p, R_EMPTY); }
        char e = *p->cur;
        if (e == 'b' || e == 'B') { p->cur++; return nn(p, e == 'b' ? R_WORDB : R_NWORDB); }
        if (e >= '1' && e <= '9') { int v = 0; while (p->cur < p->e && wu_isdigit(*p->cur)) { if (v < 1000) v = v * 10 + (*p->cur - '0'); p->cur++; } struct rnode *n = nn(p, R_BACK); n->a = v; return n; }
        if (e == 'k' && p->cur + 1 < p->e && p->cur[1] == '<') {
            p->cur += 2; const char *nm = p->cur; while (p->cur < p->e && *p->cur != '>') p->cur++;
            struct rnode *n = nn(p, R_NBACK); n->name = wa_strndup(p->ar, nm, (uint32_t)(p->cur - nm)); if (p->cur < p->e) p->cur++; return n;
        }
        struct rnode *cn = nn(p, R_CLASS); char cls = 0; uint32_t cp = read_escape(p, 0, &cls, cn);
        if (cls == 'p') return cn;
        if (cls) { class_esc(p, cn, cls); return cn; }
        struct rnode *n = nn(p, R_CHAR); n->a = (int32_t)cp; return n;
    }
    uint32_t cp = read_cp(p); struct rnode *n = nn(p, R_CHAR); n->a = (int32_t)cp; return n;
}
static struct rnode *parse_seq(struct rp *p)
{
    struct rnode *head = 0, *tail = 0;
    while (p->cur < p->e && *p->cur != '|' && *p->cur != ')' && !p->failed) {
        int g0 = p->re->ngroups;
        struct rnode *a = parse_atom(p);
        if (p->failed) break;
        if (p->cur < p->e && (*p->cur == '*' || *p->cur == '+' || *p->cur == '?' || *p->cur == '{')) {
            int32_t mn = 0, mx = -1; const char *after = 0; int ok = 1;
            switch (*p->cur) {
            case '*': mn = 0; mx = -1; after = p->cur + 1; break;
            case '+': mn = 1; mx = -1; after = p->cur + 1; break;
            case '?': mn = 0; mx = 1; after = p->cur + 1; break;
            default: ok = quant_at(p, &mn, &mx, &after); break;
            }
            if (ok) {
                p->cur = after;
                if (a->t == R_BOL || a->t == R_EOL || a->t == R_WORDB || a->t == R_NWORDB) { fail(p, "nada que repetir"); break; }
                if (mx >= 0 && mx < mn) { fail(p, "cuantificador fuera de orden"); break; }
                struct rnode *r = nn(p, R_REPEAT); r->alts = a; r->min = mn; r->max = mx; r->greedy = 1;
                if (p->cur < p->e && *p->cur == '?') { r->greedy = 0; p->cur++; }
                r->cap_lo = g0 + 1; r->cap_hi = p->re->ngroups;
                r->simple = (a->t == R_CHAR || a->t == R_ANY || a->t == R_CLASS);
                a = r;
            }
        }
        if (tail) tail->next = a; else head = a;
        tail = a;
    }
    if (!head) head = nn(p, R_EMPTY);
    return head;
}
static struct rnode *parse_disj(struct rp *p, struct rnode *holder)
{
    struct rnode *last = 0;
    for (;;) {
        struct rnode *s = parse_seq(p);
        if (last) last->altnext = s; else holder->alts = s;
        last = s;
        if (!p->failed && p->cur < p->e && *p->cur == '|') { p->cur++; continue; }
        break;
    }
    return holder;
}
struct wre *wre_compile(struct wa_arena *ar, const char *pat, uint32_t n, int flags, char *err, uint32_t errmax)
{
    struct wre *re = (struct wre *)wa_zalloc(ar, (uint32_t)sizeof *re);
    if (!re) { wu_copy(err, "memoria agotada", errmax); return 0; }
    struct rp p; memset(&p, 0, sizeof p);
    p.re = re; p.ar = ar; p.cur = pat; p.e = pat + n; p.err = err; p.errmax = errmax; p.uni = (flags & WRE_U) != 0;
    err[0] = 0; re->flags = flags; re->top.t = R_GROUP; re->top.a = -1; re->first = -1;
    parse_disj(&p, &re->top);
    if (!p.failed && p.cur < p.e) fail(&p, "parentesis ')' sin abrir");
    if (p.failed) return 0;
    if (!re->top.alts->altnext && re->top.alts->t == R_CHAR && re->top.alts->a < 128 && !(flags & WRE_I)) re->first = re->top.alts->a;
    if (!re->top.alts->altnext && re->top.alts->t == R_REPEAT && re->top.alts->min > 0 && re->top.alts->alts->t == R_CHAR && re->top.alts->alts->a < 128 && !(flags & WRE_I)) re->first = re->top.alts->alts->a;
    return re;
}
int wre_ngroups(const struct wre *re) { return re->ngroups; }
const char *wre_group_name(const struct wre *re, int i) { return i > 0 && i <= re->ngroups ? re->names[i] : 0; }
int wre_flags(const struct wre *re) { return re->flags; }

/* ------------------------------------------------------------------ emparejamiento */
enum { K_ACC, K_LA, K_LB, K_GROUP, K_REP };
struct cont { int kind; const struct rnode *n; const struct cont *up; int32_t cnt; uint32_t start; };
struct mc { int dlim; const struct wre *re; const char *s; uint32_t len; int32_t *caps; int icase, multi, dotall; uint32_t steps; int overflow, depth; uint32_t end; };
#define NONE 0xFFFFFFFFu
#define STEP_LIMIT 6000000u
#define DEPTH_LIMIT_NOSTACK 3000
#define DEPTH_LIMIT_STACK 400000

static int run(struct mc *c, const struct rnode *n, uint32_t i, const struct cont *k);

static uint32_t rd(const struct mc *c, uint32_t i, uint32_t *adv)
{
    uint8_t b = (uint8_t)c->s[i];
    if (b < 0x80) { *adv = 1; return b; }
    uint32_t ch = wu_utf8_next(c->s + i, adv); if (!*adv) *adv = 1;
    return ch;
}
static int in_rs(const struct rnode *n, uint32_t ch) { for (int i = 0; i < n->nrs; i++) if (ch >= n->rs[i].lo && ch <= n->rs[i].hi) return 1; return 0; }
static uint32_t step1(const struct mc *c, const struct rnode *n, uint32_t i)
{
    if (i >= c->len) return NONE;
    uint32_t adv, ch = rd(c, i, &adv);
    switch (n->t) {
    case R_CHAR: if ((uint32_t)n->a == ch || (c->icase && fold(ch) == fold((uint32_t)n->a))) return i + adv; return NONE;
    case R_ANY: if (c->dotall || !(ch == '\n' || ch == '\r' || ch == 0x2028 || ch == 0x2029)) return i + adv; return NONE;
    case R_CLASS: {
        int h = in_rs(n, ch);
        if (!h && c->icase) { uint32_t f = fold(ch), u = upper(ch); h = (f != ch && in_rs(n, f)) || (u != ch && in_rs(n, u)); }
        return (h != n->neg) ? i + adv : NONE;
    }
    default: return NONE;
    }
}
static uint32_t prev_start(const struct mc *c, uint32_t j) { do { j--; } while (j > 0 && ((uint8_t)c->s[j] & 0xC0) == 0x80); (void)c; return j; }
static int word_at(const struct mc *c, uint32_t i) { return i < c->len && is_word((uint8_t)c->s[i]); }
static int word_before(const struct mc *c, uint32_t i) { return i > 0 && is_word((uint8_t)c->s[i - 1]); }
static int is_nl_before(const struct mc *c, uint32_t i) { return i > 0 && (c->s[i - 1] == '\n' || c->s[i - 1] == '\r'); }
static int is_nl_at(const struct mc *c, uint32_t i) { return i < c->len && (c->s[i] == '\n' || c->s[i] == '\r'); }

static int finish(struct mc *c, uint32_t i, const struct cont *k);
static int rep_try(struct mc *c, const struct rnode *n, int32_t cnt, uint32_t i, const struct cont *up);

static int rep_iter(struct mc *c, const struct rnode *n, int32_t cnt, uint32_t i, const struct cont *up)
{
    struct cont nk; nk.kind = K_REP; nk.n = n; nk.up = up; nk.cnt = cnt + 1; nk.start = i;
    int32_t sv[32]; int lo = n->cap_lo, hi = n->cap_hi, ns = 0;
    if (hi >= lo && (hi - lo + 1) * 2 <= 32) { for (int g = lo; g <= hi; g++) { sv[ns++] = c->caps[2 * g]; sv[ns++] = c->caps[2 * g + 1]; c->caps[2 * g] = c->caps[2 * g + 1] = -1; } }
    int r = run(c, n->alts, i, &nk);
    if (!r && ns) { ns = 0; for (int g = lo; g <= hi; g++) { c->caps[2 * g] = sv[ns++]; c->caps[2 * g + 1] = sv[ns++]; } }
    return r;
}
static int rep_try(struct mc *c, const struct rnode *n, int32_t cnt, uint32_t i, const struct cont *up)
{
    if (cnt < n->min) return rep_iter(c, n, cnt, i, up);
    if (n->max >= 0 && cnt >= n->max) return run(c, n->next, i, up);
    if (n->greedy) { if (rep_iter(c, n, cnt, i, up)) return 1; if (c->overflow) return 0; return run(c, n->next, i, up); }
    if (run(c, n->next, i, up)) return 1;
    if (c->overflow) return 0;
    return rep_iter(c, n, cnt, i, up);
}
static int finish(struct mc *c, uint32_t i, const struct cont *k)
{
    switch (k->kind) {
    case K_ACC: c->end = i; return 1;
    case K_LA: return 1;
    case K_LB: return i == k->start;
    case K_GROUP: {
        const struct rnode *g = k->n; int gi = g->a;
        if (gi < 0) return run(c, g->next, i, k->up);
        int32_t o0 = c->caps[2 * gi], o1 = c->caps[2 * gi + 1];
        c->caps[2 * gi] = (int32_t)k->start; c->caps[2 * gi + 1] = (int32_t)i;
        if (run(c, g->next, i, k->up)) return 1;
        c->caps[2 * gi] = o0; c->caps[2 * gi + 1] = o1; return 0;
    }
    case K_REP:
        if (i == k->start && k->cnt > k->n->min) return 0;           /* iteracion vacia */
        return rep_try(c, k->n, k->cnt, i, k->up);
    }
    return 0;
}
static int look(struct mc *c, const struct rnode *n, uint32_t i) __attribute__((noinline));
static int look(struct mc *c, const struct rnode *n, uint32_t i)
{
    int32_t sv[2 * (WRE_MAXG + 1)]; int nc = 2 * (c->re->ngroups + 1);
    memcpy(sv, c->caps, (uint32_t)nc * sizeof(int32_t));
    int ok = 0;
    for (const struct rnode *a = n->alts; a && !ok; a = a->altnext) {
        if (!n->behind) { struct cont k = { K_LA, 0, 0, 0, 0 }; ok = run(c, a, i, &k); }
        else {
            struct cont k = { K_LB, 0, 0, 0, i };
            for (uint32_t j = i;; ) { if (run(c, a, j, &k)) { ok = 1; break; } if (c->overflow || j == 0) break; j = prev_start(c, j); }
        }
        if (c->overflow) return 0;
    }
    if (!ok || n->neg) memcpy(c->caps, sv, (uint32_t)nc * sizeof(int32_t));
    return n->neg ? !ok : ok;
}
static int backref(struct mc *c, int gi, uint32_t *i)
{
    if (gi < 1 || gi > c->re->ngroups) return 1;
    int32_t a = c->caps[2 * gi], b = c->caps[2 * gi + 1];
    if (a < 0 || b < 0) return 1;
    uint32_t n = (uint32_t)(b - a);
    if (*i + n > c->len) return 0;
    for (uint32_t t = 0; t < n; t++) {
        uint8_t x = (uint8_t)c->s[a + (int32_t)t], y = (uint8_t)c->s[*i + t];
        if (x != y && !(c->icase && x < 128 && y < 128 && fold(x) == fold(y))) return 0;
    }
    *i += n; return 1;
}
static int run(struct mc *c, const struct rnode *n, uint32_t i, const struct cont *k)
{
    if (++c->depth > c->dlim || (wre_stack_lo && (const char *)__builtin_frame_address(0) < wre_stack_lo)) { c->overflow = 1; c->depth--; return 0; }
    int res = 0;
    for (;;) {
        if (!n) { res = finish(c, i, k); break; }
        if (++c->steps > STEP_LIMIT) { c->overflow = 1; res = 0; break; }
        switch (n->t) {
        case R_EMPTY: break;
        case R_CHAR: case R_ANY: case R_CLASS: { uint32_t j = step1(c, n, i); if (j == NONE) goto out; i = j; break; }
        case R_BOL: if (!(i == 0 || (c->multi && is_nl_before(c, i)))) goto out; break;
        case R_EOL: if (!(i == c->len || (c->multi && is_nl_at(c, i)))) goto out; break;
        case R_WORDB: if (word_before(c, i) == word_at(c, i)) goto out; break;
        case R_NWORDB: if (word_before(c, i) != word_at(c, i)) goto out; break;
        case R_BACK: if (!backref(c, n->a, &i)) goto out; break;
        case R_NBACK: {
            int gi = 0; for (int g = 1; g <= c->re->ngroups; g++) if (c->re->names[g] && wu_streq(c->re->names[g], n->name)) { gi = g; break; }
            if (!backref(c, gi, &i)) goto out;
            break;
        }
        case R_LOOK: if (!look(c, n, i)) goto out; break;
        case R_GROUP: {
            struct cont nk; nk.kind = K_GROUP; nk.n = n; nk.up = k; nk.cnt = 0; nk.start = i;
            for (const struct rnode *a = n->alts; a; a = a->altnext) { if (run(c, a, i, &nk)) { res = 1; goto out; } if (c->overflow) goto out; }
            goto out;
        }
        case R_REPEAT: {
            const struct rnode *b = n->alts;
            if (n->simple) {
                uint32_t j = i; int32_t cnt = 0;
                if (n->greedy) {
                    while (n->max < 0 || cnt < n->max) { uint32_t nj = step1(c, b, j); if (nj == NONE) break; j = nj; cnt++; }
                    if (cnt < n->min) goto out;
                    for (;;) {
                        if (run(c, n->next, j, k)) { res = 1; goto out; }
                        if (c->overflow || cnt <= n->min) goto out;
                        if (++c->steps > STEP_LIMIT) { c->overflow = 1; goto out; }
                        cnt--; j = prev_start(c, j);
                    }
                }
                while (cnt < n->min) { uint32_t nj = step1(c, b, j); if (nj == NONE) goto out; j = nj; cnt++; }
                for (;;) {
                    if (run(c, n->next, j, k)) { res = 1; goto out; }
                    if (c->overflow || (n->max >= 0 && cnt >= n->max)) goto out;
                    if (++c->steps > STEP_LIMIT) { c->overflow = 1; goto out; }
                    uint32_t nj = step1(c, b, j); if (nj == NONE) goto out; j = nj; cnt++;
                }
            }
            res = rep_try(c, n, 0, i, k); goto out;
        }
        default: goto out;
        }
        n = n->next;
    }
out:
    c->depth--; return res;
}
static int attempt(struct mc *c, uint32_t pos)
{
    int nc = 2 * (c->re->ngroups + 1);
    for (int i = 0; i < nc; i++) c->caps[i] = -1;
    struct cont k = { K_ACC, 0, 0, 0, 0 };
    for (const struct rnode *a = c->re->top.alts; a; a = a->altnext) { if (run(c, a, pos, &k)) return 1; if (c->overflow) return -1; }
    return 0;
}
static void mc_init(struct mc *c, const struct wre *re, const char *s, uint32_t len, int32_t *caps)
{
    memset(c, 0, sizeof *c); c->dlim = wre_stack_lo ? DEPTH_LIMIT_STACK : DEPTH_LIMIT_NOSTACK; c->re = re; c->s = s; c->len = len; c->caps = caps;
    c->icase = (re->flags & WRE_I) != 0; c->multi = (re->flags & WRE_M) != 0; c->dotall = (re->flags & WRE_S) != 0;
}
int wre_match_at(const struct wre *re, const char *s, uint32_t len, uint32_t pos, int32_t *caps)
{
    struct mc c; mc_init(&c, re, s, len, caps);
    if (pos > len) return 0;
    int r = attempt(&c, pos);
    if (r > 0) { caps[0] = (int32_t)pos; caps[1] = (int32_t)c.end; }
    return r;
}
int wre_exec(const struct wre *re, const char *s, uint32_t len, uint32_t start, int32_t *caps)
{
    struct mc c; mc_init(&c, re, s, len, caps);
    for (uint32_t i = start; i <= len; ) {
        if (re->first >= 0 && !(re->flags & WRE_Y)) { while (i < len && (uint8_t)s[i] != (uint8_t)re->first) i++; if (i >= len) return 0; }
        int r = attempt(&c, i);
        if (r > 0) { caps[0] = (int32_t)i; caps[1] = (int32_t)c.end; return 1; }
        if (r < 0) return -1;
        if (re->flags & WRE_Y) return 0;
        if (i >= len) break;
        uint32_t adv; rd(&c, i, &adv); i += adv;
    }
    return 0;
}
