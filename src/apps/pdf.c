/* PARCHE 088: lector de PDF (ver pdf.h). Todo en enteros: numeros en coma fija 16.16 sobre int64. */
#include "pdf.h"
#include "web_util.h"
#include "web_inflate.h"
#include "web_img.h"

typedef int64_t fx;
#define FX1 ((fx)65536)
static inline fx fxm(fx a, fx b) { return (a * b) >> 16; }
static inline fx fxd(fx a, fx b) { return b ? (a << 16) / b : 0; }

/* ------------------------------------------------------------------ memoria por arena */
struct ablk { struct ablk *next; uint32_t used, cap; };
struct aown { struct aown *next; void *p; };
struct arena { struct ablk *head; struct aown *own; uint32_t total; };
static void *ar_alloc(struct arena *a, uint32_t n)
{
    n = (n + 7u) & ~7u;
    if (!a->head || a->head->used + n > a->head->cap) {
        uint32_t cap = n > 60000u ? n : 60000u;
        if (a->total > (192u << 20)) return 0;
        struct ablk *b = (struct ablk *)kmalloc(sizeof(struct ablk) + cap + 8);
        if (!b) return 0;
        b->next = a->head; b->used = 0; b->cap = cap; a->head = b; a->total += cap;
    }
    char *p = (char *)(a->head + 1) + a->head->used; a->head->used += n;
    return p;
}
static void *ar_zalloc(struct arena *a, uint32_t n) { void *p = ar_alloc(a, n); if (p) memset(p, 0, n); return p; }
static int ar_own(struct arena *a, void *p)
{
    struct aown *o = (struct aown *)ar_alloc(a, sizeof *o); if (!o) { kfree(p); return -1; }
    o->p = p; o->next = a->own; a->own = o; return 0;
}
static void ar_reset(struct arena *a)   /* vacia la arena conservando un bloque */
{
    if (!a->head) return;
    struct ablk *b = a->head->next; while (b) { struct ablk *n = b->next; kfree(b); b = n; }
    a->head->next = 0; a->head->used = 0; a->total = a->head->cap;
}
static void ar_free(struct arena *a)
{
    for (struct aown *o = a->own; o; o = o->next) kfree(o->p);
    a->own = 0;
    struct ablk *b = a->head; while (b) { struct ablk *n = b->next; kfree(b); b = n; }
    a->head = 0; a->total = 0;
}

/* ------------------------------------------------------------------ valores */
enum { V_NULL, V_BOOL, V_NUM, V_NAME, V_STR, V_ARR, V_DICT, V_REF, V_OP };
struct pv {
    uint8_t t, isint;
    int32_t n;                 /* longitud (cadena/nombre/op), elementos (arr), pares (dict), objeto (ref) */
    fx v;                      /* numero 16.16 */
    const char *s;             /* nombre / cadena / operador */
    struct pv **a;             /* arr: n elementos; dict: 2n (clave, valor) */
    const uint8_t *sd; uint32_t sl;   /* datos de flujo (crudos) */
};
static struct pv PV_NULLV = { V_NULL, 0, 0, 0, 0, 0, 0, 0 };

struct xent { uint32_t off; uint32_t stm; uint16_t idx; uint8_t type; };
struct objstm { uint32_t num; const uint8_t *d; uint32_t n; uint32_t first; int count; uint32_t *offs; uint32_t *nums; };
struct pgrec { struct pv *dict, *res; fx box[4]; int rot; };
struct pdf {
    const uint8_t *d; uint32_t n;
    struct xent *x; int nx;
    struct pv **cache; uint8_t *busy;
    struct pv *trailer;
    struct pgrec *pages; int npages, pcap;
    struct objstm stms[16]; int nstm;
    struct arena ar;
    int depth;
};

/* ------------------------------------------------------------------ lexico */
struct lex { const uint8_t *p, *e; struct pdf *doc; struct arena *ar; int content; };
static int is_ws(int c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
static int is_delim(int c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }
static void skip_ws(struct lex *L)
{
    while (L->p < L->e) {
        if (is_ws(*L->p)) { L->p++; continue; }
        if (*L->p == '%') { while (L->p < L->e && *L->p != 10 && *L->p != 13) L->p++; continue; }
        break;
    }
}
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); }
static struct pv *mk(struct lex *L, int t) { struct pv *v = (struct pv *)ar_zalloc(L->ar, sizeof *v); if (v) v->t = (uint8_t)t; return v; }
static struct pv *parse_val(struct lex *L, int depth);
static struct pv *dget(const struct pv *d, const char *key);
static struct pv *resolve(struct pdf *P, struct pv *v);

static int parse_number(struct lex *L, fx *out, int *isint)
{
    const uint8_t *p = L->p; int neg = 0; int64_t ip = 0; fx frac = 0; int any = 0, dot = 0;
    if (p < L->e && (*p == '+' || *p == '-')) { neg = *p == '-'; p++; }
    while (p < L->e && *p >= '0' && *p <= '9') { if (ip < (1LL << 40)) ip = ip * 10 + (*p - '0'); p++; any = 1; }
    if (p < L->e && *p == '.') {
        dot = 1; p++; fx scale = FX1;
        while (p < L->e && *p >= '0' && *p <= '9') { scale /= 10; frac += (*p - '0') * scale; p++; any = 1; }
    }
    if (!any) return 0;
    L->p = p;
    fx v = (ip << 16) + frac; *out = neg ? -v : v; *isint = !dot;
    return 1;
}
static struct pv *parse_string(struct lex *L)
{
    /* (literal) con escapes y parentesis anidados */
    const uint8_t *p = L->p + 1; int lvl = 1;
    uint32_t cap = 0; const uint8_t *q = p;
    while (q < L->e && lvl > 0) { if (*q == '\\') q++; else if (*q == '(') lvl++; else if (*q == ')') lvl--; q++; cap++; }
    char *o = (char *)ar_alloc(L->ar, cap + 1); if (!o) return 0;
    uint32_t k = 0; lvl = 1;
    while (p < L->e) {
        int c = *p++;
        if (c == '(') lvl++;
        else if (c == ')') { if (--lvl == 0) break; }
        else if (c == '\\' && p < L->e) {
            c = *p++;
            switch (c) {
            case 'n': c = 10; break; case 'r': c = 13; break; case 't': c = 9; break; case 'b': c = 8; break; case 'f': c = 12; break;
            case 13: if (p < L->e && *p == 10) p++; continue;
            case 10: continue;
            default:
                if (c >= '0' && c <= '7') { int v = c - '0'; for (int i = 0; i < 2 && p < L->e && *p >= '0' && *p <= '7'; i++) v = v * 8 + (*p++ - '0'); c = v & 255; }
            }
        }
        if (k < cap) o[k++] = (char)c;
    }
    o[k] = 0; L->p = p;
    struct pv *v = mk(L, V_STR); if (!v) return 0; v->s = o; v->n = (int32_t)k; return v;
}
static struct pv *parse_hexstr(struct lex *L)
{
    const uint8_t *p = L->p + 1, *q = p; while (q < L->e && *q != '>') q++;
    char *o = (char *)ar_alloc(L->ar, (uint32_t)(q - p) / 2 + 2); if (!o) return 0;
    uint32_t k = 0; int hi = -1;
    for (; p < q; p++) { int h = hexv(*p); if (h < 0) continue; if (hi < 0) hi = h; else { o[k++] = (char)(hi * 16 + h); hi = -1; } }
    if (hi >= 0) o[k++] = (char)(hi * 16);
    o[k] = 0; L->p = q < L->e ? q + 1 : q;
    struct pv *v = mk(L, V_STR); if (!v) return 0; v->s = o; v->n = (int32_t)k; return v;
}
static struct pv *parse_name(struct lex *L)
{
    const uint8_t *p = L->p + 1, *q = p; while (q < L->e && !is_ws(*q) && !is_delim(*q)) q++;
    char *o = (char *)ar_alloc(L->ar, (uint32_t)(q - p) + 1); if (!o) return 0;
    uint32_t k = 0;
    for (; p < q; p++) { if (*p == '#' && p + 2 < q + 1 && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) { o[k++] = (char)(hexv(p[1]) * 16 + hexv(p[2])); p += 2; } else o[k++] = (char)*p; }
    o[k] = 0; L->p = q;
    struct pv *v = mk(L, V_NAME); if (!v) return 0; v->s = o; v->n = (int32_t)k; return v;
}
/* lista temporal para arrays/dicts */
struct plist { struct pv **v; int n, cap; };
static int pl_push(struct plist *l, struct pv *v)
{
    if (l->n == l->cap) { int nc = l->cap ? l->cap * 2 : 16; if (nc > 1 << 20) return -1; struct pv **nv = (struct pv **)kmalloc((size_t)nc * sizeof(*nv)); if (!nv) return -1; if (l->v) { memcpy(nv, l->v, (size_t)l->n * sizeof(*nv)); kfree(l->v); } l->v = nv; l->cap = nc; }
    l->v[l->n++] = v; return 0;
}
static struct pv **pl_take(struct lex *L, struct plist *l)
{
    struct pv **a = (struct pv **)ar_alloc(L->ar, (uint32_t)(l->n + 1) * sizeof(*a));
    if (a && l->n) memcpy(a, l->v, (size_t)l->n * sizeof(*a));
    if (l->v) kfree(l->v);
    l->v = 0; l->cap = 0;
    return a;
}
static int kw(struct lex *L, const char *w)
{
    int n = (int)wu_strlen(w);
    if (L->e - L->p < n || memcmp(L->p, w, (size_t)n)) return 0;
    if (L->p + n < L->e && !is_ws(L->p[n]) && !is_delim(L->p[n])) return 0;
    L->p += n; return 1;
}
static struct pv *parse_val(struct lex *L, int depth)
{
    skip_ws(L);
    if (L->p >= L->e || depth > 64) return 0;
    int c = *L->p;
    if (c == '/') return parse_name(L);
    if (c == '(') return parse_string(L);
    if (c == '<') {
        if (L->p + 1 < L->e && L->p[1] == '<') {
            L->p += 2; struct plist l = { 0, 0, 0 };
            for (;;) {
                skip_ws(L);
                if (L->p >= L->e) break;
                if (*L->p == '>' && L->p + 1 < L->e && L->p[1] == '>') { L->p += 2; break; }
                struct pv *k = parse_val(L, depth + 1);
                if (!k) break;
                if (k->t != V_NAME) continue;
                struct pv *v = parse_val(L, depth + 1);
                if (!v) v = &PV_NULLV;
                if (pl_push(&l, k) || pl_push(&l, v)) break;
            }
            struct pv *d = mk(L, V_DICT); if (!d) { if (l.v) kfree(l.v); return 0; }
            d->n = l.n / 2; d->a = pl_take(L, &l);
            /* flujo */
            if (!L->content) {
                const uint8_t *save = L->p; skip_ws(L);
                if (kw(L, "stream")) {
                    if (L->p < L->e && *L->p == 13) L->p++;
                    if (L->p < L->e && *L->p == 10) L->p++;
                    const uint8_t *s0 = L->p; int64_t len = -1;
                    struct pv *lv = dget(d, "Length");
                    if (lv && lv->t == V_REF && L->doc) lv = resolve(L->doc, lv);
                    if (lv && lv->t == V_NUM) len = lv->v >> 16;
                    int ok = len >= 0 && s0 + len <= L->e;
                    if (ok) { const uint8_t *t = s0 + len; while (t < L->e && is_ws(*t)) t++; ok = (L->e - t >= 9 && !memcmp(t, "endstream", 9)); }
                    if (!ok) {   /* /Length erroneo: se busca endstream */
                        const uint8_t *t = s0; len = -1;
                        while (t + 9 <= L->e) { if (*t == 'e' && !memcmp(t, "endstream", 9)) { len = t - s0; break; } t++; }
                        if (len < 0) len = L->e - s0;
                        while (len > 0 && (s0[len - 1] == 10 || s0[len - 1] == 13)) len--;
                    }
                    d->sd = s0; d->sl = (uint32_t)len; L->p = s0 + len;
                } else L->p = save;
            }
            return d;
        }
        return parse_hexstr(L);
    }
    if (c == '[') {
        L->p++; struct plist l = { 0, 0, 0 };
        for (;;) {
            skip_ws(L);
            if (L->p >= L->e) break;
            if (*L->p == ']') { L->p++; break; }
            struct pv *v = parse_val(L, depth + 1);
            if (!v) { if (L->p < L->e && (*L->p == ')' || *L->p == '>' || *L->p == '}' || *L->p == '{')) { L->p++; continue; } break; }
            if (pl_push(&l, v)) break;
        }
        struct pv *a = mk(L, V_ARR); if (!a) { if (l.v) kfree(l.v); return 0; }
        a->n = l.n; a->a = pl_take(L, &l); return a;
    }
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
        fx v; int isint;
        if (!parse_number(L, &v, &isint)) { L->p++; return mk(L, V_NULL); }
        if (!L->content && isint && v >= 0) {   /* N G R */
            struct lex t = *L; skip_ws(&t); fx g; int gi;
            if (t.p < t.e && *t.p >= '0' && *t.p <= '9' && parse_number(&t, &g, &gi) && gi) {
                skip_ws(&t);
                if (t.p < t.e && *t.p == 'R' && (t.p + 1 >= t.e || is_ws(t.p[1]) || is_delim(t.p[1]))) {
                    L->p = t.p + 1; struct pv *r = mk(L, V_REF); if (r) { r->n = (int32_t)(v >> 16); r->v = g; } return r;
                }
            }
        }
        struct pv *n = mk(L, V_NUM); if (n) { n->v = v; n->isint = (uint8_t)isint; } return n;
    }
    if (c == ')' || c == '>' || c == ']' || c == '}' || c == '{') { L->p++; return 0; }
    /* palabra clave */
    const uint8_t *q = L->p; while (q < L->e && !is_ws(*q) && !is_delim(*q)) q++;
    if (q == L->p) { L->p++; return 0; }
    int n = (int)(q - L->p);
    if (n == 4 && !memcmp(L->p, "true", 4)) { L->p = q; struct pv *b = mk(L, V_BOOL); if (b) b->v = 1; return b; }
    if (n == 5 && !memcmp(L->p, "false", 5)) { L->p = q; return mk(L, V_BOOL); }
    if (n == 4 && !memcmp(L->p, "null", 4)) { L->p = q; return mk(L, V_NULL); }
    struct pv *o = mk(L, V_OP); if (o) { o->s = (const char *)L->p; o->n = n; }
    L->p = q; return o;
}

/* ------------------------------------------------------------------ objetos */
static int name_is(const struct pv *v, const char *s) { int n = (int)wu_strlen(s); return v && v->t == V_NAME && v->n == n && !memcmp(v->s, s, (size_t)n); }
static struct pv *dget_raw(const struct pv *d, const char *key)
{
    if (!d || d->t != V_DICT) return 0;
    int n = (int)wu_strlen(key);
    for (int i = 0; i < d->n; i++) { struct pv *k = d->a[2 * i]; if (k && k->n == n && !memcmp(k->s, key, (size_t)n)) return d->a[2 * i + 1]; }
    return 0;
}
static struct pv *dget(const struct pv *d, const char *key) { return dget_raw(d, key); }
static struct pv *get_obj(struct pdf *P, int num);
static struct pv *resolve(struct pdf *P, struct pv *v)
{
    for (int i = 0; v && v->t == V_REF && i < 16; i++) v = get_obj(P, v->n);
    return v && v->t == V_REF ? 0 : v;
}
static struct pv *dgetr(struct pdf *P, const struct pv *d, const char *key) { return resolve(P, dget(d, key)); }
static int num_of(struct pv *v, fx *out) { if (v && v->t == V_NUM) { *out = v->v; return 1; } return 0; }
static int64_t int_of(struct pdf *P, struct pv *v, int64_t def) { v = resolve(P, v); return v && v->t == V_NUM ? (v->v >> 16) : def; }

static int decode_stream(struct pdf *P, struct pv *s, uint8_t **out, uint32_t *olen, int *is_jpeg);
static int load_objstm(struct pdf *P, uint32_t num, struct objstm **out)
{
    for (int i = 0; i < P->nstm; i++) if (P->stms[i].num == num) { *out = &P->stms[i]; return 0; }
    struct pv *s = get_obj(P, (int)num);
    if (!s || s->t != V_DICT || !s->sd) return -1;
    uint8_t *d; uint32_t n; int jp;
    if (decode_stream(P, s, &d, &n, &jp) || jp) return -1;
    if (ar_own(&P->ar, d)) return -1;
    struct objstm *o = P->nstm < 16 ? &P->stms[P->nstm++] : &P->stms[num % 16];
    memset(o, 0, sizeof *o);
    o->num = num; o->d = d; o->n = n; o->first = (uint32_t)int_of(P, dget(s, "First"), 0);
    int cnt = (int)int_of(P, dget(s, "N"), 0); if (cnt < 0 || cnt > 100000) cnt = 0;
    o->offs = (uint32_t *)ar_alloc(&P->ar, (uint32_t)(cnt + 1) * 4); o->nums = (uint32_t *)ar_alloc(&P->ar, (uint32_t)(cnt + 1) * 4);
    if (!o->offs || !o->nums) return -1;
    struct lex L = { d, d + n, P, &P->ar, 1 };
    for (int i = 0; i < cnt; i++) {
        fx a, b; int ia, ib; skip_ws(&L);
        if (!parse_number(&L, &a, &ia)) break;
        skip_ws(&L);
        if (!parse_number(&L, &b, &ib)) break;
        o->nums[i] = (uint32_t)(a >> 16); o->offs[i] = (uint32_t)(b >> 16); o->count = i + 1;
    }
    *out = o; return 0;
}
static struct pv *get_obj(struct pdf *P, int num)
{
    if (num <= 0 || num >= P->nx) return 0;
    if (P->cache[num]) return P->cache[num];
    if (P->busy[num] || P->depth > 48) return 0;
    struct xent *e = &P->x[num]; struct pv *v = 0;
    P->busy[num] = 1; P->depth++;
    if (e->type == 1 && e->off < P->n) {
        struct lex L = { P->d + e->off, P->d + P->n, P, &P->ar, 0 };
        fx a, b; int ia, ib; skip_ws(&L);
        if (parse_number(&L, &a, &ia)) { skip_ws(&L); if (parse_number(&L, &b, &ib)) { skip_ws(&L); if (kw(&L, "obj")) v = parse_val(&L, 0); } }
    } else if (e->type == 2) {
        struct objstm *o;
        if (load_objstm(P, e->stm, &o) == 0) {
            int k = -1;
            if (e->idx < o->count && o->nums[e->idx] == (uint32_t)num) k = e->idx;
            else for (int i = 0; i < o->count; i++) if (o->nums[i] == (uint32_t)num) { k = i; break; }
            if (k >= 0 && o->first + o->offs[k] < o->n) {
                struct lex L = { o->d + o->first + o->offs[k], o->d + o->n, P, &P->ar, 0 };
                v = parse_val(&L, 0);
            }
        }
    }
    P->depth--; P->busy[num] = 0;
    if (v) P->cache[num] = v;
    return v;
}

/* ------------------------------------------------------------------ filtros */
static int a85(const uint8_t *s, uint32_t n, struct wb *o)
{
    uint32_t acc = 0; int k = 0;
    for (uint32_t i = 0; i < n; i++) {
        int c = s[i];
        if (c == '~') break;
        if (is_ws(c)) continue;
        if (c == 'z' && k == 0) { wb_addn(o, "\0\0\0\0", 4); continue; }
        if (c < '!' || c > 'u') return -1;
        acc = acc * 85 + (uint32_t)(c - '!'); k++;
        if (k == 5) { char b[4] = { (char)(acc >> 24), (char)(acc >> 16), (char)(acc >> 8), (char)acc }; wb_addn(o, b, 4); acc = 0; k = 0; }
    }
    if (k > 1) { for (int j = k; j < 5; j++) acc = acc * 85 + 84; char b[4] = { (char)(acc >> 24), (char)(acc >> 16), (char)(acc >> 8), (char)acc }; wb_addn(o, b, (uint32_t)(k - 1)); }
    return 0;
}
static int ahx(const uint8_t *s, uint32_t n, struct wb *o)
{
    int hi = -1;
    for (uint32_t i = 0; i < n; i++) { if (s[i] == '>') break; int h = hexv(s[i]); if (h < 0) continue; if (hi < 0) hi = h; else { char c = (char)(hi * 16 + h); wb_addn(o, &c, 1); hi = -1; } }
    if (hi >= 0) { char c = (char)(hi * 16); wb_addn(o, &c, 1); }
    return 0;
}
static int lzw(const uint8_t *s, uint32_t n, struct wb *o, int early)
{
    /* LZW de PDF (codigos de 9 a 12 bits, 256 = borrar, 257 = fin) */
    static uint16_t pre[4096]; static uint8_t suf[4096], first[4096]; static uint16_t len[4096];
    uint8_t stack[4097];
    for (int i = 0; i < 256; i++) { pre[i] = 0xFFFF; suf[i] = (uint8_t)i; first[i] = (uint8_t)i; len[i] = 1; }
    int next = 258, bits = 9, old = -1; uint32_t bitbuf = 0; int nb = 0; uint32_t i = 0;
    for (;;) {
        while (nb < bits && i < n) { bitbuf = (bitbuf << 8) | s[i++]; nb += 8; }
        if (nb < bits) break;
        int code = (int)((bitbuf >> (nb - bits)) & ((1u << bits) - 1)); nb -= bits;
        if (code == 257) break;
        if (code == 256) { next = 258; bits = 9; old = -1; continue; }
        int cur = code; uint8_t fc;
        if (code < next && (code < 256 || code >= 258)) fc = first[code];
        else if (code == next && old >= 0) fc = first[old];
        else return -1;
        if (old >= 0 && next < 4096) { pre[next] = (uint16_t)old; suf[next] = code == next ? fc : first[code]; first[next] = first[old]; len[next] = (uint16_t)(len[old] + 1); next++; }
        int sp = 0; for (int c = cur; c != 0xFFFF && sp < 4096; c = pre[c]) { stack[sp++] = suf[c]; if (c < 256) break; }
        for (int j = sp - 1; j >= 0; j--) wb_addn(o, (const char *)&stack[j], 1);
        old = code;
        if (next + early >= (1 << bits) && bits < 12) bits++;
        if (o->err) return -1;
    }
    return 0;
}
static int predictor(struct wb *b, int pred, int colors, int bpc, int cols)
{
    if (pred < 2) return 0;
    if (colors < 1) colors = 1;
    if (bpc < 1) bpc = 8;
    if (cols < 1) cols = 1;
    if (colors > 32 || bpc > 16 || cols > 1 << 20) return -1;
    int bpp = (colors * bpc + 7) / 8; uint32_t row = (uint32_t)((cols * colors * bpc + 7) / 8);
    uint8_t *d = (uint8_t *)b->p;
    if (pred == 2) {
        if (bpc != 8) return 0;
        for (uint32_t r = 0; r + row <= b->len; r += row) for (uint32_t i = (uint32_t)bpp; i < row; i++) d[r + i] = (uint8_t)(d[r + i] + d[r + i - (uint32_t)bpp]);
        return 0;
    }
    uint32_t nrows = b->len / (row + 1); uint8_t *prev = 0; uint32_t w = 0;
    for (uint32_t r = 0; r < nrows; r++) {
        uint8_t *src = d + r * (row + 1); int f = src[0]; uint8_t *cur = d + w;
        memmove(cur, src + 1, row);
        for (uint32_t i = 0; i < row; i++) {
            int a = i >= (uint32_t)bpp ? cur[i - (uint32_t)bpp] : 0, up = prev ? prev[i] : 0, c = (prev && i >= (uint32_t)bpp) ? prev[i - (uint32_t)bpp] : 0;
            switch (f) {
            case 1: cur[i] = (uint8_t)(cur[i] + a); break;
            case 2: cur[i] = (uint8_t)(cur[i] + up); break;
            case 3: cur[i] = (uint8_t)(cur[i] + ((a + up) >> 1)); break;
            case 4: { int p = a + up - c, pa = p > a ? p - a : a - p, pb = p > up ? p - up : up - p, pc = p > c ? p - c : c - p; cur[i] = (uint8_t)(cur[i] + ((pa <= pb && pa <= pc) ? a : (pb <= pc ? up : c))); } break;
            default: break;
            }
        }
        prev = cur; w += row;
    }
    b->len = w;
    return 0;
}
/* decodifica un flujo; DCT/JPX se devuelven tal cual con *is_jpeg = 1/2 */
static int decode_stream(struct pdf *P, struct pv *s, uint8_t **out, uint32_t *olen, int *is_jpeg)
{
    *out = 0; *olen = 0; *is_jpeg = 0;
    if (!s || !s->sd) return -1;
    struct pv *f = dgetr(P, s, "Filter"), *dp = dgetr(P, s, "DecodeParms");
    if (!dp) dp = dgetr(P, s, "DP");
    struct pv *one[1]; struct pv **fl = 0; int nf = 0;
    if (f && f->t == V_NAME) { one[0] = f; fl = one; nf = 1; }
    else if (f && f->t == V_ARR) { fl = f->a; nf = f->n; }
    struct wb cur; memset(&cur, 0, sizeof cur);
    wb_addn(&cur, (const char *)s->sd, s->sl);
    if (cur.err || !cur.p) { wb_free(&cur); return -1; }
    for (int i = 0; i < nf; i++) {
        struct pv *fn = resolve(P, fl[i]);
        struct pv *parm = dp && dp->t == V_ARR ? (i < dp->n ? resolve(P, dp->a[i]) : 0) : dp;
        struct wb nx; memset(&nx, 0, sizeof nx); int r = 0;
        if (name_is(fn, "FlateDecode") || name_is(fn, "Fl")) {
            r = wz_inflate((const uint8_t *)cur.p, cur.len, 3, &nx, 128u << 20);
            if (r < 0 && nx.len > 0) r = 0;                  /* flujo truncado: lo que haya */
            if (!r && parm) r = predictor(&nx, (int)int_of(P, dget(parm, "Predictor"), 1), (int)int_of(P, dget(parm, "Colors"), 1), (int)int_of(P, dget(parm, "BitsPerComponent"), 8), (int)int_of(P, dget(parm, "Columns"), 1));
        } else if (name_is(fn, "LZWDecode") || name_is(fn, "LZW")) {
            r = lzw((const uint8_t *)cur.p, cur.len, &nx, parm ? (int)int_of(P, dget(parm, "EarlyChange"), 1) : 1);
            if (!r && parm) r = predictor(&nx, (int)int_of(P, dget(parm, "Predictor"), 1), (int)int_of(P, dget(parm, "Colors"), 1), (int)int_of(P, dget(parm, "BitsPerComponent"), 8), (int)int_of(P, dget(parm, "Columns"), 1));
        } else if (name_is(fn, "ASCII85Decode") || name_is(fn, "A85")) r = a85((const uint8_t *)cur.p, cur.len, &nx);
        else if (name_is(fn, "ASCIIHexDecode") || name_is(fn, "AHx")) r = ahx((const uint8_t *)cur.p, cur.len, &nx);
        else if (name_is(fn, "DCTDecode") || name_is(fn, "DCT")) { *is_jpeg = 1; wb_free(&nx); break; }
        else if (name_is(fn, "JPXDecode")) { *is_jpeg = 2; wb_free(&nx); break; }
        else { wb_free(&nx); wb_free(&cur); return -2; }   /* CCITT, JBIG2, RunLength... */
        wb_free(&cur);
        if (r < 0 || nx.err) { wb_free(&nx); return -1; }
        if (!nx.p) wb_addn(&nx, "", 0);
        cur = nx;
    }
    *out = (uint8_t *)cur.p; *olen = cur.len;
    return *out ? 0 : -1;
}

/* ------------------------------------------------------------------ xref */
static int xref_grow(struct pdf *P, int n)
{
    if (n <= P->nx) return 0;
    if (n > 4000000) return -1;
    int nc = P->nx ? P->nx : 64; while (nc < n) nc *= 2;
    struct xent *x = (struct xent *)kmalloc((size_t)nc * sizeof *x); struct pv **c = (struct pv **)kmalloc((size_t)nc * sizeof *c); uint8_t *b = (uint8_t *)kmalloc((size_t)nc);
    if (!x || !c || !b) { if (x) kfree(x); if (c) kfree(c); if (b) kfree(b); return -1; }
    memset(x, 0, (size_t)nc * sizeof *x); memset(c, 0, (size_t)nc * sizeof *c); memset(b, 0, (size_t)nc);
    if (P->x) { memcpy(x, P->x, (size_t)P->nx * sizeof *x); memcpy(c, P->cache, (size_t)P->nx * sizeof *c); kfree(P->x); kfree(P->cache); kfree(P->busy); }
    P->x = x; P->cache = c; P->busy = b; P->nx = nc;
    return 0;
}
static void xset(struct pdf *P, uint32_t num, int type, uint32_t a, uint32_t b)
{
    if (num == 0 || num > 3999999 || xref_grow(P, (int)num + 1)) return;
    if (P->x[num].type) return;                               /* la seccion mas nueva manda */
    P->x[num].type = (uint8_t)type;
    if (type == 1) P->x[num].off = a; else { P->x[num].stm = a; P->x[num].idx = (uint16_t)b; }
}
static int load_xref(struct pdf *P, uint32_t off, int depth)
{
    if (depth > 32 || off >= P->n) return -1;
    struct lex L = { P->d + off, P->d + P->n, P, &P->ar, 0 };
    skip_ws(&L);
    struct pv *trailer = 0;
    if (kw(&L, "xref")) {
        for (;;) {
            skip_ws(&L); fx a, b; int ia, ib;
            const uint8_t *save = L.p;
            if (!parse_number(&L, &a, &ia)) { L.p = save; break; }
            skip_ws(&L);
            if (!parse_number(&L, &b, &ib)) return -1;
            int64_t start = a >> 16, cnt = b >> 16;
            if (start < 0 || cnt < 0 || cnt > 4000000) return -1;
            for (int64_t i = 0; i < cnt; i++) {
                fx o, g; int io, ig; skip_ws(&L);
                if (!parse_number(&L, &o, &io)) return -1;
                skip_ws(&L);
                if (!parse_number(&L, &g, &ig)) return -1;
                skip_ws(&L);
                if (L.p >= L.e) return -1;
                int t = *L.p++;
                if (t == 'n' && start + i > 0) xset(P, (uint32_t)(start + i), 1, (uint32_t)(o >> 16), 0);
                else if (t == 'f' && start + i > 0 && start + i < 4000000 && !xref_grow(P, (int)(start + i) + 1) && !P->x[start + i].type) P->x[start + i].type = 3;   /* libre: ocupa el sitio */
            }
        }
        skip_ws(&L);
        if (!kw(&L, "trailer")) return -1;
        trailer = parse_val(&L, 0);
        if (!trailer || trailer->t != V_DICT) return -1;
        struct pv *xs = dget(trailer, "XRefStm");
        if (xs && xs->t == V_NUM) load_xref(P, (uint32_t)(xs->v >> 16), depth + 1);
    } else {
        fx a, b; int ia, ib;
        if (!parse_number(&L, &a, &ia)) return -1;
        skip_ws(&L);
        if (!parse_number(&L, &b, &ib)) return -1;
        skip_ws(&L);
        if (!kw(&L, "obj")) return -1;
        struct pv *s = parse_val(&L, 0);
        if (!s || s->t != V_DICT || !s->sd || !name_is(dget(s, "Type"), "XRef")) return -1;
        struct pv *W = dget(s, "W");
        if (!W || W->t != V_ARR || W->n < 3) return -1;
        int w[3]; for (int i = 0; i < 3; i++) { w[i] = W->a[i] && W->a[i]->t == V_NUM ? (int)(W->a[i]->v >> 16) : 0; if (w[i] < 0 || w[i] > 8) return -1; }
        uint8_t *d; uint32_t n; int jp;
        if (decode_stream(P, s, &d, &n, &jp)) return -1;
        int size = (int)int_of(P, dget(s, "Size"), 0);
        struct pv *idx = dget(s, "Index");
        int64_t ranges[2 * 64]; int nr = 0;
        if (idx && idx->t == V_ARR) for (int i = 0; i + 1 < idx->n && nr < 64; i += 2) { ranges[2 * nr] = idx->a[i]->v >> 16; ranges[2 * nr + 1] = idx->a[i + 1]->v >> 16; nr++; }
        else { ranges[0] = 0; ranges[1] = size; nr = 1; }
        uint32_t pos = 0, rec = (uint32_t)(w[0] + w[1] + w[2]);
        for (int r = 0; r < nr; r++) {
            for (int64_t i = 0; i < ranges[2 * r + 1] && rec && pos + rec <= n; i++) {
                uint64_t f[3]; for (int k = 0; k < 3; k++) { f[k] = 0; for (int j = 0; j < w[k]; j++) f[k] = (f[k] << 8) | d[pos++]; }
                int type = w[0] ? (int)f[0] : 1; int64_t num = ranges[2 * r] + i;
                if (num <= 0 || num >= 4000000) continue;
                if (type == 1) xset(P, (uint32_t)num, 1, (uint32_t)f[1], 0);
                else if (type == 2) xset(P, (uint32_t)num, 2, (uint32_t)f[1], (uint32_t)f[2]);
                else if (!xref_grow(P, (int)num + 1) && !P->x[num].type) P->x[num].type = 3;
            }
        }
        kfree(d);
        trailer = s;
    }
    if (!P->trailer) P->trailer = trailer;
    struct pv *prev = dget(trailer, "Prev");
    if (prev && prev->t == V_NUM && (prev->v >> 16) != off) load_xref(P, (uint32_t)(prev->v >> 16), depth + 1);
    return 0;
}
/* reconstruccion: se buscan todos los "N G obj" del archivo */
static void brute_scan(struct pdf *P)
{
    for (int i = 0; i < P->nx; i++) { P->x[i].type = 0; P->cache[i] = 0; }
    P->nstm = 0;
    const uint8_t *d = P->d; uint32_t n = P->n; struct pv *last_trailer = 0;
    for (uint32_t i = 0; i + 3 < n; i++) {
        if (d[i] == 'o' && d[i + 1] == 'b' && d[i + 2] == 'j' && i > 3 && is_ws(d[i - 1])) {
            uint32_t j = i - 1; while (j > 0 && is_ws(d[j])) j--;
            uint32_t ge = j + 1; while (j > 0 && d[j] >= '0' && d[j] <= '9') j--;
            if (j + 1 >= ge || !is_ws(d[j])) continue;
            while (j > 0 && is_ws(d[j])) j--;
            uint32_t ne = j + 1; while (j > 0 && d[j] >= '0' && d[j] <= '9') j--;
            uint32_t ns = (d[j] >= '0' && d[j] <= '9') ? j : j + 1;
            if (ns >= ne || ne - ns > 7) continue;
            uint32_t num = 0; for (uint32_t k = ns; k < ne; k++) num = num * 10 + (uint32_t)(d[k] - '0');
            if (num == 0 || xref_grow(P, (int)num + 1)) continue;
            P->x[num].type = 1; P->x[num].off = ns;          /* el ultimo gana (actualizaciones incrementales) */
        } else if (d[i] == 't' && i + 7 < n && !memcmp(d + i, "trailer", 7)) {
            struct lex L = { d + i + 7, d + n, P, &P->ar, 0 }; struct pv *t = parse_val(&L, 0);
            if (t && t->t == V_DICT && dget(t, "Root")) last_trailer = t;
        }
    }
    /* objetos dentro de flujos de objetos */
    int nx = P->nx;
    for (int num = 1; num < nx; num++) {
        if (P->x[num].type != 1) continue;
        struct pv *o = get_obj(P, num);
        if (!o || o->t != V_DICT || !name_is(dget(o, "Type"), "ObjStm")) continue;
        struct objstm *s; if (load_objstm(P, (uint32_t)num, &s)) continue;
        for (int k = 0; k < s->count; k++) if (s->nums[k] && s->nums[k] < 4000000 && !xref_grow(P, (int)s->nums[k] + 1) && !P->x[s->nums[k]].type) { P->x[s->nums[k]].type = 2; P->x[s->nums[k]].stm = (uint32_t)num; P->x[s->nums[k]].idx = (uint16_t)k; }
    }
    if (last_trailer) P->trailer = last_trailer;
    else {   /* sin trailer: se busca el catalogo */
        for (int num = 1; num < P->nx; num++) {
            if (!P->x[num].type) continue;
            struct pv *o = get_obj(P, num);
            if (o && o->t == V_DICT && name_is(dget(o, "Type"), "Catalog")) {
                struct lex L = { (const uint8_t *)"<< >>", (const uint8_t *)"<< >>" + 5, P, &P->ar, 0 };
                struct pv *t = parse_val(&L, 0); if (!t) break;
                struct pv **a = (struct pv **)ar_alloc(&P->ar, 2 * sizeof(*a)); struct pv *k = (struct pv *)ar_zalloc(&P->ar, sizeof *k), *r = (struct pv *)ar_zalloc(&P->ar, sizeof *r);
                if (!a || !k || !r) break;
                k->t = V_NAME; k->s = "Root"; k->n = 4; r->t = V_REF; r->n = num; a[0] = k; a[1] = r; t->a = a; t->n = 1;
                P->trailer = t; break;
            }
        }
    }
}

/* ------------------------------------------------------------------ paginas */
static void collect_pages(struct pdf *P, struct pv *node, struct pv *res, fx *box, int rot, int depth)
{
    if (!node || node->t != V_DICT || depth > 40 || P->npages >= 20000) return;
    struct pv *r = dgetr(P, node, "Resources"); if (r && r->t == V_DICT) res = r;
    fx b[4]; memcpy(b, box, sizeof b);
    struct pv *mb = dgetr(P, node, "MediaBox"), *cb = dgetr(P, node, "CropBox");
    struct pv *bx = cb && cb->t == V_ARR && cb->n >= 4 ? cb : mb;
    if (bx && bx->t == V_ARR && bx->n >= 4) for (int i = 0; i < 4; i++) { struct pv *e = resolve(P, bx->a[i]); if (e && e->t == V_NUM) b[i] = e->v; }
    struct pv *ro = dgetr(P, node, "Rotate"); if (ro && ro->t == V_NUM) rot = (int)(ro->v >> 16);
    struct pv *kids = dgetr(P, node, "Kids");
    if (kids && kids->t == V_ARR && !name_is(dget(node, "Type"), "Page")) {
        for (int i = 0; i < kids->n; i++) {
            struct pv *k = kids->a[i];
            if (k && k->t == V_REF) { if (k->n <= 0 || k->n >= P->nx || P->busy[k->n]) continue; struct pv *kd = resolve(P, k); P->busy[k->n] = 1; collect_pages(P, kd, res, b, rot, depth + 1); P->busy[k->n] = 0; }
            else collect_pages(P, k, res, b, rot, depth + 1);
        }
        return;
    }
    if (P->npages == P->pcap) {
        int nc = P->pcap ? P->pcap * 2 : 16; struct pgrec *np = (struct pgrec *)kmalloc((size_t)nc * sizeof *np); if (!np) return;
        if (P->pages) { memcpy(np, P->pages, (size_t)P->npages * sizeof *np); kfree(P->pages); }
        P->pages = np; P->pcap = nc;
    }
    struct pgrec *pg = &P->pages[P->npages++];
    pg->dict = node; pg->res = res; memcpy(pg->box, b, sizeof b); pg->rot = ((rot % 360) + 360) % 360;
    if (pg->box[2] < pg->box[0]) { fx t = pg->box[0]; pg->box[0] = pg->box[2]; pg->box[2] = t; }
    if (pg->box[3] < pg->box[1]) { fx t = pg->box[1]; pg->box[1] = pg->box[3]; pg->box[3] = t; }
    if (pg->box[2] - pg->box[0] < FX1 * 8 || pg->box[3] - pg->box[1] < FX1 * 8) { pg->box[0] = 0; pg->box[1] = 0; pg->box[2] = 612 * FX1; pg->box[3] = 792 * FX1; }
}
static int find_pages(struct pdf *P)
{
    struct pv *root = dgetr(P, P->trailer, "Root");
    struct pv *pages = root ? dgetr(P, root, "Pages") : 0;
    if (!pages) return -1;
    fx box[4] = { 0, 0, 612 * FX1, 792 * FX1 };
    collect_pages(P, pages, 0, box, 0, 0);
    return P->npages > 0 ? 0 : -1;
}

struct pdf *pdf_open(const uint8_t *data, uint32_t len, char *err, int errmax)
{
    if (err && errmax) err[0] = 0;
    #define FAIL(m) do { if (err) wu_copy(err, m, (uint32_t)errmax); pdf_close(P); return 0; } while (0)
    struct pdf *P = (struct pdf *)kmalloc(sizeof *P); if (!P) { if (err) wu_copy(err, "sin memoria", (uint32_t)errmax); return 0; }
    memset(P, 0, sizeof *P); P->d = data; P->n = len;
    uint32_t h = 0; while (h + 5 < len && h < 1024 && memcmp(data + h, "%PDF-", 5)) h++;
    if (h + 5 >= len || h >= 1024) FAIL("no es un PDF");
    if (xref_grow(P, 64)) FAIL("sin memoria");
    /* startxref */
    int ok = 0;
    for (uint32_t i = len > 9 ? len - 9 : 0, lim = len > 2048 ? len - 2048 : 0; i > lim; i--) {
        if (!memcmp(data + i, "startxref", 9)) {
            struct lex L = { data + i + 9, data + len, P, &P->ar, 0 }; fx o; int io; skip_ws(&L);
            if (parse_number(&L, &o, &io) && load_xref(P, (uint32_t)(o >> 16), 0) == 0) ok = 1;
            break;
        }
    }
    if (ok && P->trailer && dget(P->trailer, "Encrypt")) FAIL("PDF cifrado (todavia no soportado)");
    if (!ok || !P->trailer || find_pages(P) != 0) {
        P->trailer = 0; P->npages = 0; brute_scan(P);
        if (!P->trailer) FAIL("PDF danado: no se encuentra el catalogo");
        if (dget(P->trailer, "Encrypt")) FAIL("PDF cifrado (todavia no soportado)");
        if (find_pages(P) != 0) FAIL("PDF sin paginas legibles");
    }
    return P;
    #undef FAIL
}
void pdf_close(struct pdf *P)
{
    if (!P) return;
    ar_free(&P->ar);
    if (P->x) kfree(P->x);
    if (P->cache) kfree(P->cache);
    if (P->busy) kfree(P->busy);
    if (P->pages) kfree(P->pages);
    kfree(P);
}
int pdf_page_count(const struct pdf *P) { return P ? P->npages : 0; }
int pdf_page_size(struct pdf *P, int i, int *w, int *h)
{
    if (!P || i < 0 || i >= P->npages) return -1;
    struct pgrec *g = &P->pages[i];
    int pw = (int)((g->box[2] - g->box[0]) >> 16), ph = (int)((g->box[3] - g->box[1]) >> 16);
    if (g->rot == 90 || g->rot == 270) { int t = pw; pw = ph; ph = t; }
    *w = pw; *h = ph; return 0;
}

/* ------------------------------------------------------------------ texto: UTF-8 */
static int put_u8(char *o, uint32_t cp)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 63)); o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63)); return 4;
}
int pdf_title(struct pdf *P, char *out, int max)
{
    if (!P || max < 2) return 0;
    out[0] = 0;
    struct pv *info = dgetr(P, P->trailer, "Info"), *t = info ? dgetr(P, info, "Title") : 0;
    if (!t || t->t != V_STR || !t->n) return 0;
    int k = 0; const uint8_t *s = (const uint8_t *)t->s;
    if (t->n >= 2 && s[0] == 0xFE && s[1] == 0xFF) {
        for (int i = 2; i + 1 < t->n && k < max - 5; i += 2) { uint32_t c = ((uint32_t)s[i] << 8) | s[i + 1]; if (c >= 0xD800 && c < 0xDC00 && i + 3 < t->n) { uint32_t c2 = ((uint32_t)s[i + 2] << 8) | s[i + 3]; c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); i += 2; } if (c >= 32) k += put_u8(out + k, c); }
    } else for (int i = 0; i < t->n && k < max - 3; i++) if (s[i] >= 32) k += put_u8(out + k, s[i]);
    out[k] = 0; return k;
}

static const uint16_t winansi80[32] = { 0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };
static const uint16_t macroman80[128] = {
    0xC4,0xC5,0xC7,0xC9,0xD1,0xD6,0xDC,0xE1,0xE0,0xE2,0xE4,0xE3,0xE5,0xE7,0xE9,0xE8,0xEA,0xEB,0xED,0xEC,0xEE,0xEF,0xF1,0xF3,0xF2,0xF4,0xF6,0xF5,0xFA,0xF9,0xFB,0xFC,
    0x2020,0xB0,0xA2,0xA3,0xA7,0x2022,0xB6,0xDF,0xAE,0xA9,0x2122,0xB4,0xA8,0x2260,0xC6,0xD8,0x221E,0xB1,0x2264,0x2265,0xA5,0xB5,0x2202,0x2211,0x220F,0x3C0,0x222B,0xAA,0xBA,0x3A9,0xE6,0xF8,
    0xBF,0xA1,0xAC,0x221A,0x192,0x2248,0x2206,0xAB,0xBB,0x2026,0xA0,0xC0,0xC3,0xD5,0x152,0x153,0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0xF7,0x25CA,0xFF,0x178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
    0x2021,0xB7,0x201A,0x201E,0x2030,0xC2,0xCA,0xC1,0xCB,0xC8,0xCD,0xCE,0xCF,0xCC,0xD3,0xD4,0xF8FF,0xD2,0xDA,0xDB,0xD9,0x131,0x2C6,0x2DC,0xAF,0x2D8,0x2D9,0x2DA,0xB8,0x2DD,0x2DB,0x2C7 };
struct gname { const char *n; uint16_t u; };
static const struct gname gnames[] = {
    {"space",32},{"exclam",33},{"quotedbl",34},{"numbersign",35},{"dollar",36},{"percent",37},{"ampersand",38},{"quotesingle",39},{"quoteright",0x2019},
    {"parenleft",40},{"parenright",41},{"asterisk",42},{"plus",43},{"comma",44},{"hyphen",45},{"period",46},{"slash",47},{"zero",48},{"one",49},{"two",50},
    {"three",51},{"four",52},{"five",53},{"six",54},{"seven",55},{"eight",56},{"nine",57},{"colon",58},{"semicolon",59},{"less",60},{"equal",61},{"greater",62},
    {"question",63},{"at",64},{"bracketleft",91},{"backslash",92},{"bracketright",93},{"asciicircum",94},{"underscore",95},{"grave",96},{"quoteleft",0x2018},
    {"braceleft",123},{"bar",124},{"braceright",125},{"asciitilde",126},{"bullet",0x2022},{"endash",0x2013},{"emdash",0x2014},{"ellipsis",0x2026},
    {"quotedblleft",0x201C},{"quotedblright",0x201D},{"quotesinglbase",0x201A},{"quotedblbase",0x201E},{"guillemotleft",0xAB},{"guillemotright",0xBB},
    {"guilsinglleft",0x2039},{"guilsinglright",0x203A},{"fi",0xFB01},{"fl",0xFB02},{"ff",0xFB00},{"ffi",0xFB03},{"ffl",0xFB04},{"dagger",0x2020},
    {"daggerdbl",0x2021},{"section",0xA7},{"paragraph",0xB6},{"degree",0xB0},{"copyright",0xA9},{"registered",0xAE},{"trademark",0x2122},{"Euro",0x20AC},
    {"euro",0x20AC},{"sterling",0xA3},{"yen",0xA5},{"cent",0xA2},{"minus",0x2212},{"multiply",0xD7},{"divide",0xF7},{"plusminus",0xB1},{"germandbls",0xDF},
    {"exclamdown",0xA1},{"questiondown",0xBF},{"ordfeminine",0xAA},{"ordmasculine",0xBA},{"dotlessi",0x131},{"periodcentered",0xB7},{"AE",0xC6},{"ae",0xE6},
    {"OE",0x152},{"oe",0x153},{"Oslash",0xD8},{"oslash",0xF8},{"Eth",0xD0},{"eth",0xF0},{"Thorn",0xDE},{"thorn",0xFE},{"mu",0xB5},{"nbspace",0xA0},
    {"sfthyphen",0xAD},{"logicalnot",0xAC},{"brokenbar",0xA6},{"dieresis",0xA8},{"acute",0xB4},{"cedilla",0xB8},{"macron",0xAF},{"circumflex",0x2C6},
    {"tilde",0x2DC},{"onehalf",0xBD},{"onequarter",0xBC},{"threequarters",0xBE},{"onesuperior",0xB9},{"twosuperior",0xB2},{"threesuperior",0xB3},
    {"perthousand",0x2030},{"florin",0x192},{"fraction",0x2044},{"infinity",0x221E},{"lessequal",0x2264},{"greaterequal",0x2265},{"notequal",0x2260},
    {"arrowright",0x2192},{"arrowleft",0x2190},{"alpha",0x3B1},{"beta",0x3B2},{"gamma",0x3B3},{"delta",0x3B4},{"epsilon",0x3B5},{"pi",0x3C0},{"sigma",0x3C3},
    {"lambda",0x3BB},{"theta",0x3B8},{"omega",0x3C9},{"summation",0x2211},{"integral",0x222B},{"radical",0x221A},{"partialdiff",0x2202},{"Delta",0x394},
    {"Omega",0x3A9},{"approxequal",0x2248},{"dotaccent",0x2D9},{"ring",0x2DA},{"breve",0x2D8},{"caron",0x2C7},{"ogonek",0x2DB},{"hungarumlaut",0x2DD},
    {0,0} };
static uint32_t glyph_uni(const char *n, int len)
{
    if (len == 1) return (uint8_t)n[0];
    if (len >= 7 && !memcmp(n, "uni", 3)) { uint32_t v = 0; for (int i = 3; i < 7; i++) { int h = hexv(n[i]); if (h < 0) return 0; v = v * 16 + (uint32_t)h; } return v; }
    if (len >= 5 && n[0] == 'u' && len <= 7) { uint32_t v = 0; for (int i = 1; i < len; i++) { int h = hexv(n[i]); if (h < 0) { v = 0; break; } v = v * 16 + (uint32_t)h; } if (v) return v; }
    for (const struct gname *g = gnames; g->n; g++) if ((int)wu_strlen(g->n) == len && !memcmp(g->n, n, (size_t)len)) return g->u;
    /* letra + acento (Latin-1) */
    static const char *acc[] = { "grave", "acute", "circumflex", "tilde", "dieresis", "ring", "cedilla", 0 };
    static const char *base_up = "AAAAAAC", *base_lo = "aaaaaac";
    (void)base_up; (void)base_lo;
    for (int a = 0; acc[a]; a++) {
        int al = (int)wu_strlen(acc[a]);
        if (len == al + 1 && !memcmp(n + 1, acc[a], (size_t)al)) {
            char c = n[0]; int up = c >= 'A' && c <= 'Z'; char l = up ? (char)(c + 32) : c;
            static const uint16_t A[] = { 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0 }, E[] = { 0xE8, 0xE9, 0xEA, 0, 0xEB, 0, 0 }, I[] = { 0xEC, 0xED, 0xEE, 0, 0xEF, 0, 0 },
                O[] = { 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0, 0 }, U[] = { 0xF9, 0xFA, 0xFB, 0, 0xFC, 0, 0 }, N[] = { 0, 0, 0, 0xF1, 0, 0, 0 }, C[] = { 0, 0, 0, 0, 0, 0, 0xE7 }, Y[] = { 0, 0xFD, 0, 0, 0xFF, 0, 0 };
            const uint16_t *t = l == 'a' ? A : l == 'e' ? E : l == 'i' ? I : l == 'o' ? O : l == 'u' ? U : l == 'n' ? N : l == 'c' ? C : l == 'y' ? Y : 0;
            if (t && t[a]) { uint32_t cp = t[a]; if (up) cp = cp == 0xFF ? 0x178 : cp - 32; return cp; }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ fuentes */
struct tu { uint32_t lo, hi; uint32_t cp; char u8[12]; uint8_t n; };   /* rango -> cp + (codigo - lo); n>0: cadena fija */
struct font {
    struct pv *key;
    int two;                   /* codigos de 2 bytes */
    int flags;
    uint32_t uni[256];
    uint16_t w[256];           /* simples: ancho/1000 */
    int dw;                    /* CID: ancho por defecto */
    struct { uint32_t a, b; uint16_t w; } *cw; int ncw;
    struct tu *tu; int ntu;
    int missing;
    fx fm[6]; int type3;
};
static int hexs_val(const struct pv *s, uint32_t *v) { if (!s || s->t != V_STR || s->n < 1 || s->n > 4) return 0; uint32_t x = 0; for (int i = 0; i < s->n; i++) x = (x << 8) | (uint8_t)s->s[i]; *v = x; return s->n; }
static int utf16_to_u8(const struct pv *s, char *o, int max)
{
    int k = 0; const uint8_t *b = (const uint8_t *)s->s;
    for (int i = 0; i + 1 < s->n && k < max - 4; i += 2) {
        uint32_t c = ((uint32_t)b[i] << 8) | b[i + 1];
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < s->n) { uint32_t c2 = ((uint32_t)b[i + 2] << 8) | b[i + 3]; c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); i += 2; }
        k += put_u8(o + k, c);
    }
    return k;
}
static void parse_tounicode(struct pdf *P, struct font *F, struct pv *s, struct arena *ar)
{
    uint8_t *d; uint32_t n; int jp;
    if (decode_stream(P, s, &d, &n, &jp) || jp) return;
    if (ar_own(ar, d)) return;
    struct lex L = { d, d + n, P, ar, 1 };
    struct plist ent = { 0, 0, 0 }; (void)ent;
    int cap = 64, cnt = 0; struct tu *t = (struct tu *)kmalloc((size_t)cap * sizeof *t); if (!t) return;
    struct pv *st[3]; int ns = 0, mode = 0;
    for (int guard = 0; guard < 2000000 && L.p < L.e; guard++) {
        struct pv *v = parse_val(&L, 0);
        if (!v) { if (L.p >= L.e) break; continue; }
        if (v->t == V_OP) {
            if (v->n == 19 && !memcmp(v->s, "begincodespacerange", 19)) mode = 1;
            else if (v->n == 11 && !memcmp(v->s, "beginbfchar", 11)) mode = 2;
            else if (v->n == 12 && !memcmp(v->s, "beginbfrange", 12)) mode = 3;
            else if (v->n >= 3 && !memcmp(v->s, "end", 3)) mode = 0;
            ns = 0; continue;
        }
        if (!mode) continue;
        if (mode == 1) continue;                          /* el tamano del codigo lo decide el tipo de fuente, no el CMap */
        if (ns < 3) st[ns++] = v;
        int need = mode == 2 ? 2 : 3;
        if (ns < need) continue;
        ns = 0;
        if (cnt == cap) { if (cap > 200000) break; struct tu *nt = (struct tu *)kmalloc((size_t)cap * 2 * sizeof *nt); if (!nt) break; memcpy(nt, t, (size_t)cnt * sizeof *t); kfree(t); t = nt; cap *= 2; }
        struct tu *e = &t[cnt]; memset(e, 0, sizeof *e);
        uint32_t lo, hi;
        if (!hexs_val(st[0], &lo)) continue;
        if (mode == 2) {
            e->lo = e->hi = lo;
            if (st[1]->t == V_STR) { if (st[1]->n == 2) e->cp = ((uint32_t)(uint8_t)st[1]->s[0] << 8) | (uint8_t)st[1]->s[1]; else e->n = (uint8_t)utf16_to_u8(st[1], e->u8, sizeof e->u8); }
            else if (st[1]->t == V_NAME) e->cp = glyph_uni(st[1]->s, st[1]->n);
            cnt++;
        } else {
            if (!hexs_val(st[1], &hi) || hi < lo || hi - lo > 65535) continue;
            if (st[2]->t == V_STR) {
                e->lo = lo; e->hi = hi;
                if (st[2]->n == 2) e->cp = ((uint32_t)(uint8_t)st[2]->s[0] << 8) | (uint8_t)st[2]->s[1];
                else if (st[2]->n == 4) { char tmp[12]; int k = utf16_to_u8(st[2], tmp, sizeof tmp); uint32_t cp = 0; const uint8_t *q = (const uint8_t *)tmp; if (k == 4) cp = ((q[0] & 7u) << 18) | ((q[1] & 63u) << 12) | ((q[2] & 63u) << 6) | (q[3] & 63u); else if (k) { e->n = (uint8_t)k; memcpy(e->u8, tmp, (size_t)k); } e->cp = cp; }
                else { e->n = (uint8_t)utf16_to_u8(st[2], e->u8, sizeof e->u8); e->hi = lo; }
                cnt++;
            } else if (st[2]->t == V_ARR) {
                for (int i = 0; i < st[2]->n && lo + (uint32_t)i <= hi; i++) {
                    if (cnt == cap) { if (cap > 200000) break; struct tu *nt = (struct tu *)kmalloc((size_t)cap * 2 * sizeof *nt); if (!nt) break; memcpy(nt, t, (size_t)cnt * sizeof *t); kfree(t); t = nt; cap *= 2; }
                    struct tu *x = &t[cnt]; memset(x, 0, sizeof *x); x->lo = x->hi = lo + (uint32_t)i;
                    struct pv *s2 = st[2]->a[i]; if (s2 && s2->t == V_STR) { if (s2->n == 2) x->cp = ((uint32_t)(uint8_t)s2->s[0] << 8) | (uint8_t)s2->s[1]; else x->n = (uint8_t)utf16_to_u8(s2, x->u8, sizeof x->u8); }
                    cnt++;
                }
            }
        }
    }
    if (ar_own(ar, t)) return;
    F->tu = t; F->ntu = cnt;
}
static int has_sub(const char *h, const char *n) { for (; *h; h++) { int i = 0; while (n[i] && h[i] == n[i]) i++; if (!n[i]) return 1; } return 0; }
static void font_style(struct font *F, struct pv *bf)
{
    if (!bf || bf->t != V_NAME) return;
    const char *s = bf->s; int n = bf->n;
    if (n > 7 && s[6] == '+') { s += 7; n -= 7; }
    char low[64]; int k = 0; for (int i = 0; i < n && k < 63; i++) { char c = s[i]; low[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c); }
    low[k] = 0;
    #define HAS(x) has_sub(low, x)
    if (HAS("bold") || HAS("black") || HAS("heavy") || HAS("semibold") || HAS("demi") || (low[0] == 'c' && low[1] == 'm' && low[2] == 'b')) F->flags |= PDF_TF_BOLD;
    if (HAS("italic") || HAS("oblique") || (low[0] == 'c' && low[1] == 'm' && (low[2] == 't' && low[3] == 'i'))) F->flags |= PDF_TF_ITALIC;
    if (HAS("courier") || HAS("mono") || HAS("consol") || (low[0] == 'c' && low[1] == 'm' && low[2] == 't' && low[3] == 't')) F->flags |= PDF_TF_MONO;
    if (HAS("times") || HAS("serif") || HAS("roman") || HAS("georgia") || HAS("garamond") || (low[0] == 'c' && low[1] == 'm' && low[2] != 's')) F->flags |= PDF_TF_SERIF;
    if (HAS("sans")) F->flags &= ~PDF_TF_SERIF;
    #undef HAS
}
/* codificaciones propias de las fuentes Computer Modern de TeX (cuando el PDF no trae /Encoding ni ToUnicode) */
struct cmmap { uint8_t c; uint16_t u; };
static const struct cmmap cm_sy[] = { {0x00,0x2212},{0x01,0xB7},{0x02,0xD7},{0x03,0x2217},{0x04,0xF7},{0x06,0xB1},{0x07,0x2213},{0x08,0x2295},{0x0F,0x2022},
    {0x10,0x224D},{0x11,0x2261},{0x12,0x2286},{0x13,0x2287},{0x14,0x2264},{0x15,0x2265},{0x18,0x223C},{0x19,0x2248},{0x1A,0x2282},{0x1B,0x2283},{0x1C,0x226A},{0x1D,0x226B},
    {0x20,0x2190},{0x21,0x2192},{0x22,0x2191},{0x23,0x2193},{0x24,0x2194},{0x29,0x21D2},{0x2C,0x21D4},{0x30,0x2032},{0x31,0x221E},{0x32,0x2208},{0x33,0x220B},
    {0x38,0x2200},{0x39,0x2203},{0x3A,0xAC},{0x3B,0x2205},{0x3E,0x22A5},{0x5B,0x222A},{0x5C,0x2229},{0x5E,0x2227},{0x5F,0x2228},{0x66,'{'},{0x67,'}'},
    {0x68,0x27E8},{0x69,0x27E9},{0x6A,'|'},{0x6B,0x2016},{0x6E,'\\'},{0x70,0x221A},{0x72,0x2207},{0x73,0x222B},{0x7C,0x2663},{0x7D,0x2662},{0x7E,0x2661},{0x7F,0x2660},{0,0} };
static const struct cmmap cm_ex[] = { {0x00,'('},{0x01,')'},{0x02,'['},{0x03,']'},{0x08,'{'},{0x09,'}'},{0x0A,0x27E8},{0x0B,0x27E9},{0x10,'('},{0x11,')'},{0x12,'('},{0x13,')'},
    {0x14,'['},{0x15,']'},{0x1A,'{'},{0x1B,'}'},{0x20,'('},{0x21,')'},{0x22,'['},{0x23,']'},{0x28,'{'},{0x29,'}'},{0x50,0x2211},{0x51,0x220F},{0x52,0x222B},{0x53,0x22C3},
    {0x58,0x2211},{0x59,0x220F},{0x5A,0x222B},{0x5B,0x22C3},{0x70,0x221A},{0x71,0x221A},{0x72,0x221A},{0x73,0x221A},{0,0} };
static const uint16_t cm_greek_lo[23] = { 0x3B1,0x3B2,0x3B3,0x3B4,0x3F5,0x3B6,0x3B7,0x3B8,0x3B9,0x3BA,0x3BB,0x3BC,0x3BD,0x3BE,0x3C0,0x3C1,0x3C3,0x3C4,0x3C5,0x3D5,0x3C7,0x3C8,0x3C9 };
static const uint16_t cm_greek_up[11] = { 0x393,0x394,0x398,0x39B,0x39E,0x3A0,0x3A3,0x3A5,0x3A6,0x3A8,0x3A9 };
static void cm_encoding(struct font *F, const char *bn, int n)
{
    if (n > 7 && bn[6] == '+') { bn += 7; n -= 7; }
    if (n < 4 || bn[0] != 'C' || bn[1] != 'M') return;
    const struct cmmap *m = 0;
    if (!memcmp(bn, "CMSY", 4) || !memcmp(bn, "CMBSY", 5)) m = cm_sy;
    else if (!memcmp(bn, "CMEX", 4)) m = cm_ex;
    if (m) { for (int i = 0; i < 128; i++) F->uni[i] = i >= 0x41 && i <= 0x5A ? (uint32_t)i : 0x2219; for (; m->u; m++) F->uni[m->c] = m->u; return; }
    for (int i = 0; i < 11; i++) F->uni[i] = cm_greek_up[i];
    if (!memcmp(bn, "CMMI", 4)) { for (int i = 0; i < 23; i++) F->uni[0x0B + i] = cm_greek_lo[i]; F->uni[0x3A] = '.'; F->uni[0x3B] = ','; F->uni[0x3C] = '<'; F->uni[0x3D] = '/'; F->uni[0x3E] = '>'; F->uni[0x40] = 0x2202; F->uni[0x7B] = 0x131; F->uni[0x7C] = 0x237; return; }
    F->uni[0x0B] = 0xFB00; F->uni[0x0C] = 0xFB01; F->uni[0x0D] = 0xFB02; F->uni[0x0E] = 0xFB03; F->uni[0x0F] = 0xFB04; F->uni[0x10] = 0x131;
    F->uni[0x22] = 0x201D; F->uni[0x5C] = 0x201C; F->uni[0x27] = 0x2019; F->uni[0x60] = 0x2018; F->uni[0x7B] = 0x2013; F->uni[0x7C] = 0x2014; F->uni[0x19] = 0xDF; F->uni[0x1A] = 0xE6; F->uni[0x1B] = 0x153; F->uni[0x1C] = 0xF8;
}
static struct font *load_font(struct pdf *P, struct pv *fd, struct arena *ar)
{
    struct font *F = (struct font *)ar_zalloc(ar, sizeof *F); if (!F) return 0;
    F->key = fd; F->dw = 1000; F->missing = 500;
    struct pv *sub = dgetr(P, fd, "Subtype");
    font_style(F, dgetr(P, fd, "BaseFont"));
    for (int i = 0; i < 256; i++) { F->uni[i] = (uint32_t)i; F->w[i] = 0xFFFF; }
    for (int i = 0x80; i < 0xA0; i++) F->uni[i] = winansi80[i - 0x80];
    if (name_is(sub, "Type0")) {
        F->two = 1;
        struct pv *df = dgetr(P, fd, "DescendantFonts"); struct pv *cf = df && df->t == V_ARR && df->n ? resolve(P, df->a[0]) : 0;
        if (cf) {
            struct pv *fdsc = dgetr(P, cf, "FontDescriptor"); (void)fdsc;
            if (!(F->flags & (PDF_TF_BOLD | PDF_TF_ITALIC))) font_style(F, dgetr(P, cf, "BaseFont"));
            F->dw = (int)int_of(P, dget(cf, "DW"), 1000);
            struct pv *W = dgetr(P, cf, "W");
            if (W && W->t == V_ARR) {
                int cap = W->n + 8; F->cw = ar_alloc(ar, (uint32_t)cap * sizeof(*F->cw) * 4); int nc = 0, capn = cap * 4;
                for (int i = 0; F->cw && i < W->n; ) {
                    int64_t c1 = int_of(P, W->a[i], -1);
                    struct pv *nx = i + 1 < W->n ? resolve(P, W->a[i + 1]) : 0;
                    if (nx && nx->t == V_ARR) {
                        for (int j = 0; j < nx->n && nc < capn; j++) { F->cw[nc].a = F->cw[nc].b = (uint32_t)(c1 + j); F->cw[nc].w = (uint16_t)int_of(P, nx->a[j], F->dw); nc++; }
                        i += 2;
                    } else if (i + 2 < W->n) {
                        int64_t c2 = int_of(P, W->a[i + 1], -1); if (nc < capn) { F->cw[nc].a = (uint32_t)c1; F->cw[nc].b = (uint32_t)c2; F->cw[nc].w = (uint16_t)int_of(P, W->a[i + 2], F->dw); nc++; }
                        i += 3;
                    } else break;
                }
                F->ncw = nc;
            }
        }
    } else {
        int t3 = name_is(sub, "Type3"); F->type3 = t3;
        if (t3) {
            struct pv *m = dgetr(P, fd, "FontMatrix");
            for (int i = 0; i < 6; i++) F->fm[i] = (m && m->t == V_ARR && i < m->n && m->a[i]->t == V_NUM) ? m->a[i]->v : (i == 0 || i == 3 ? FX1 / 1000 : 0);
        }
        struct pv *enc = dgetr(P, fd, "Encoding"), *base = enc;
        { struct pv *bf = dgetr(P, fd, "BaseFont"); if (bf && bf->t == V_NAME && (!enc || (enc->t == V_DICT && !dget(enc, "BaseEncoding")))) cm_encoding(F, bf->s, bf->n); }
        if (enc && enc->t == V_DICT) base = dgetr(P, enc, "BaseEncoding");
        if (name_is(base, "MacRomanEncoding")) for (int i = 0x80; i < 256; i++) F->uni[i] = macroman80[i - 0x80];
        else if (name_is(base, "StandardEncoding")) { F->uni[0x27] = 0x2019; F->uni[0x60] = 0x2018; }
        if (enc && enc->t == V_DICT) {
            struct pv *dif = dgetr(P, enc, "Differences");
            int code = 0;
            if (dif && dif->t == V_ARR) for (int i = 0; i < dif->n; i++) {
                struct pv *e = dif->a[i];
                if (e->t == V_NUM) code = (int)(e->v >> 16);
                else if (e->t == V_NAME) { if (code >= 0 && code < 256) { uint32_t u = glyph_uni(e->s, e->n); if (u) F->uni[code] = u; } code++; }
            }
        }
        int fc = (int)int_of(P, dget(fd, "FirstChar"), 0);
        struct pv *W = dgetr(P, fd, "Widths");
        struct pv *desc = dgetr(P, fd, "FontDescriptor");
        if (desc) { F->missing = (int)int_of(P, dget(desc, "MissingWidth"), 0); if (!F->missing) F->missing = (int)int_of(P, dget(desc, "AvgWidth"), 500); if (F->missing <= 0) F->missing = 500; }
        if (W && W->t == V_ARR) for (int i = 0; i < W->n && fc + i < 256; i++) if (fc + i >= 0) {
            struct pv *e = resolve(P, W->a[i]); fx v = e && e->t == V_NUM ? e->v : 0;
            if (t3) v = fxm(v, F->fm[0]) * 1000;                     /* Type3: unidades de glifo -> milesimas */
            int w = (int)(v >> 16); F->w[fc + i] = (uint16_t)(w < 0 ? 0 : w > 4000 ? 4000 : w);
        }
        if (F->flags & PDF_TF_MONO) for (int i = 0; i < 256; i++) if (F->w[i] == 0xFFFF) F->w[i] = 600;
    }
    struct pv *tu = dgetr(P, fd, "ToUnicode");
    if (tu && tu->t == V_DICT && tu->sd) parse_tounicode(P, F, tu, ar);
    return F;
}
static int font_width(const struct font *F, uint32_t code)
{
    if (F->two) { for (int i = 0; i < F->ncw; i++) if (code >= F->cw[i].a && code <= F->cw[i].b) return F->cw[i].w; return F->dw; }
    int w = F->w[code & 255]; return w == 0xFFFF ? F->missing : w;
}
static int font_uni(const struct font *F, uint32_t code, char *o)
{
    for (int i = 0; i < F->ntu; i++) {
        const struct tu *t = &F->tu[i];
        if (code < t->lo || code > t->hi) continue;
        if (t->n) { memcpy(o, t->u8, t->n); return t->n; }
        uint32_t cp = t->cp + (code - t->lo);
        if (cp == 0 || cp == 0xFFFD) break;
        return put_u8(o, cp);
    }
    if (F->two) return code < 0x80 && code >= 0x20 ? put_u8(o, code) : put_u8(o, '?');
    uint32_t u = F->uni[code & 255]; if (!u) u = code & 255;
    if (u < 32) u = ' ';
    return put_u8(o, u);
}

/* ------------------------------------------------------------------ interprete */
struct gst {
    fx ctm[6]; uint32_t fillc, strokec; fx lw;
    struct font *font; fx fs, tc, tw, th, tl, rise; int tr;
    int fill_pat;
};
struct fcache { struct pv *key; struct font *f; };
struct icache { struct pv *key; uint32_t *px; int w, h; };
struct rctx {
    struct pdf *P; struct pdf_page *pg; struct arena *ar; struct arena scr;
    struct gst gs[33]; int gsp;
    fx tm[6], tlm[6];
    int32_t *path; int np, pcap; fx cx, cy, sx, sy; int has_cur;
    struct fcache fc[64]; int nfc;
    struct icache ic[64]; int nic;
    int depth; uint32_t nops_budget;
    /* palabra en curso */
    char wbuf[512]; int wl; fx wx, wy, wend; int wsize; uint32_t wcol; int wflags; int wfont;
};
static void mat_mul(const fx *a, const fx *b, fx *o)   /* o = a x b */
{
    fx r[6];
    r[0] = fxm(a[0], b[0]) + fxm(a[1], b[2]); r[1] = fxm(a[0], b[1]) + fxm(a[1], b[3]);
    r[2] = fxm(a[2], b[0]) + fxm(a[3], b[2]); r[3] = fxm(a[2], b[1]) + fxm(a[3], b[3]);
    r[4] = fxm(a[4], b[0]) + fxm(a[5], b[2]) + b[4]; r[5] = fxm(a[4], b[1]) + fxm(a[5], b[3]) + b[5];
    memcpy(o, r, sizeof r);
}
static void xform(const fx *m, fx x, fx y, fx *ox, fx *oy) { *ox = fxm(x, m[0]) + fxm(y, m[2]) + m[4]; *oy = fxm(x, m[1]) + fxm(y, m[3]) + m[5]; }
static fx isqrt64(fx v) { if (v <= 0) return 0; uint64_t r = 0, b = 1ULL << 62; uint64_t x = (uint64_t)v; while (b > x) b >>= 2; while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (fx)r; }
static fx vlen(fx a, fx b) { return isqrt64(fxm(a, a) + fxm(b, b)) << 8; }   /* |(a,b)| en 16.16 */
static struct pdf_op *new_op(struct rctx *R, int kind)
{
    struct pdf_page *pg = R->pg;
    if (!R->nops_budget) return 0;
    R->nops_budget--;
    if (pg->nops == pg->cap) {
        int nc = pg->cap ? pg->cap * 2 : 256; struct pdf_op *n = (struct pdf_op *)kmalloc((size_t)nc * sizeof *n); if (!n) return 0;
        if (pg->ops) { memcpy(n, pg->ops, (size_t)pg->nops * sizeof *n); kfree(pg->ops); }
        pg->ops = n; pg->cap = nc;
    }
    struct pdf_op *o = &pg->ops[pg->nops++]; memset(o, 0, sizeof *o); o->kind = (uint8_t)kind; return o;
}
static void flush_word(struct rctx *R)
{
    if (R->wl <= 0) { R->wl = 0; return; }
    struct pdf_op *o = new_op(R, PDF_OP_TEXT);
    if (o) {
        char *t = (char *)ar_alloc(R->ar, (uint32_t)R->wl + 1);
        if (t) { memcpy(t, R->wbuf, (size_t)R->wl); t[R->wl] = 0; }
        o->text = t; o->len = t ? R->wl : 0;
        o->x = (int32_t)(R->wx >> 16); o->y = (int32_t)(R->wy >> 16); o->w = (int32_t)((R->wend - R->wx) >> 16); if (o->w < 1) o->w = 1;
        o->size = (int16_t)(R->wsize > 2000 ? 2000 : R->wsize); o->color = R->wcol; o->flags = (uint8_t)R->wflags; o->font = (uint8_t)R->wfont;
    }
    R->wl = 0;
}
static void show_text(struct rctx *R, const struct pv *s)
{
    struct gst *g = &R->gs[R->gsp]; struct font *F = g->font;
    if (!F || s->t != V_STR) return;
    fx trm[6]; mat_mul(R->tm, g->ctm, trm);
    fx ys = vlen(trm[2], trm[3]); int size = (int)(fxm(ys, g->fs) >> 16); if (size < 0) size = -size;
    int invisible = g->tr == 3 || g->tr == 7 || size < 1;
    const uint8_t *b = (const uint8_t *)s->s;
    for (int i = 0; i < s->n; ) {
        uint32_t code; int one;
        if (F->two && i + 1 < s->n) { code = ((uint32_t)b[i] << 8) | b[i + 1]; i += 2; one = 0; } else { code = b[i++]; one = 1; }
        char u[16]; int ul = font_uni(F, code, u);
        int w = font_width(F, code);
        int sp = one && code == 32;
        int is_space = ul == 1 && (u[0] == ' ' || u[0] == '\t');
        fx ox, oy; xform(R->tm, 0, g->rise, &ox, &oy); fx dx, dy; xform(g->ctm, ox, oy, &dx, &dy);
        fx adv = fxm(fxm((fx)w * FX1 / 1000, g->fs) + g->tc + (sp ? g->tw : 0), g->th);
        if (!invisible) {
            if (is_space) flush_word(R);
            else {
                if (R->wl > 0 && (R->wcol != g->fillc || R->wsize != size || R->wflags != F->flags || R->wl + ul >= (int)sizeof R->wbuf - 1 ||
                                  (dx - R->wend > (fx)size * FX1 / 5) || (R->wend - dx > (fx)size * FX1 / 2) || (dy - R->wy > FX1 * 2) || (R->wy - dy > FX1 * 2))) flush_word(R);
                if (R->wl == 0) { R->wx = dx; R->wy = dy; R->wcol = g->fillc; R->wsize = size; R->wflags = F->flags; R->wfont = 0; for (int q = 0; q < R->nfc; q++) if (R->fc[q].f == F) { R->wfont = q + 1; break; } }
                memcpy(R->wbuf + R->wl, u, (size_t)ul); R->wl += ul;
                fx ex, ey; xform(R->tm, adv, g->rise, &ex, &ey); fx ddx, ddy; xform(g->ctm, ex, ey, &ddx, &ddy);
                R->wend = ddx; (void)ddy;
            }
        }
        R->tm[4] += fxm(adv, R->tm[0]); R->tm[5] += fxm(adv, R->tm[1]);
    }
}
static void tj_adjust(struct rctx *R, fx n)
{
    struct gst *g = &R->gs[R->gsp];
    fx adv = -fxm(fxm(n / 1000, g->fs), g->th);
    if (n < -180 * FX1 || n > 400 * FX1) flush_word(R);
    R->tm[4] += fxm(adv, R->tm[0]); R->tm[5] += fxm(adv, R->tm[1]);
}
static void path_pt(struct rctx *R, fx x, fx y)
{
    struct gst *g = &R->gs[R->gsp]; fx dx, dy; xform(g->ctm, x, y, &dx, &dy);
    if (R->np + 2 > R->pcap) {
        int nc = R->pcap ? R->pcap * 2 : 1024; if (nc > (1 << 22)) return;
        int32_t *n = (int32_t *)kmalloc((size_t)nc * 4); if (!n) return;
        if (R->path) { memcpy(n, R->path, (size_t)R->np * 4); kfree(R->path); }
        R->path = n; R->pcap = nc;
    }
    int64_t px = dx >> 12, py = dy >> 12;
    if (px > 0x3FFFFFFF) px = 0x3FFFFFFF;
    if (px < -0x3FFFFFFF) px = -0x3FFFFFFF;
    if (py > 0x3FFFFFFF) py = 0x3FFFFFFF;
    if (py < -0x3FFFFFFF) py = -0x3FFFFFFF;
    R->path[R->np++] = (int32_t)px; R->path[R->np++] = (int32_t)py;
}
static void path_break(struct rctx *R) { if (R->np >= 2 && !(R->path[R->np - 2] == -1 && R->path[R->np - 1] == -1)) { if (R->np + 2 <= R->pcap || R->pcap == 0) { path_pt(R, 0, 0); R->path[R->np - 2] = -1; R->path[R->np - 1] = -1; } } }
static void curve(struct rctx *R, fx x1, fx y1, fx x2, fx y2, fx x3, fx y3)
{
    fx x0 = R->cx, y0 = R->cy;
    for (int i = 1; i <= 10; i++) {
        fx t = (fx)i * FX1 / 10, u = FX1 - t;
        fx a = fxm(fxm(u, u), u), b = 3 * fxm(fxm(u, u), t), c = 3 * fxm(fxm(u, t), t), d = fxm(fxm(t, t), t);
        path_pt(R, fxm(a, x0) + fxm(b, x1) + fxm(c, x2) + fxm(d, x3), fxm(a, y0) + fxm(b, y1) + fxm(c, y2) + fxm(d, y3));
    }
    R->cx = x3; R->cy = y3;
}
static void paint(struct rctx *R, int fill, int stroke, int eo)
{
    if (R->np >= 4 && (fill || stroke)) {
        struct gst *g = &R->gs[R->gsp];
        struct pdf_op *o = new_op(R, PDF_OP_PATH);
        int32_t *pts = o ? (int32_t *)ar_alloc(R->ar, (uint32_t)R->np * 4) : 0;
        if (o && pts) {
            memcpy(pts, R->path, (size_t)R->np * 4);
            o->pts = pts; o->npts = R->np / 2;
            o->flags = (uint8_t)((fill && !g->fill_pat ? 1 : 0) | (stroke ? 2 : 0) | (eo ? 4 : 0));
            o->fill = g->fillc; o->color = g->strokec;
            fx lw = fxm(g->lw, vlen(g->ctm[0], g->ctm[1])); int l16 = (int)(lw >> 12); if (l16 < 12) l16 = 12; if (l16 > 30000) l16 = 30000;
            o->size = (int16_t)l16;
            int32_t x0 = 0x7FFFFFFF, y0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y1 = -0x7FFFFFFF;
            for (int i = 0; i < R->np; i += 2) { if (pts[i] == -1 && pts[i + 1] == -1) continue; if (pts[i] < x0) x0 = pts[i]; if (pts[i] > x1) x1 = pts[i]; if (pts[i + 1] < y0) y0 = pts[i + 1]; if (pts[i + 1] > y1) y1 = pts[i + 1]; }
            o->x = x0 >> 4; o->y = y0 >> 4; o->w = ((x1 - x0) >> 4) + 1; o->h = ((y1 - y0) >> 4) + 1;
            if (!o->flags) R->pg->nops--;
        } else if (o) R->pg->nops--;
    }
    R->np = 0; R->has_cur = 0;
}
static uint32_t rgb_of(struct pv **st, int n, int start)
{
    fx c[4] = { 0, 0, 0, 0 }; int k = n - start; if (k > 4) k = 4;
    for (int i = 0; i < k; i++) if (st[start + i] && st[start + i]->t == V_NUM) c[i] = st[start + i]->v;
    #define C8(v) ((uint32_t)((v) < 0 ? 0 : (v) >= FX1 ? 255 : ((v) * 255) >> 16))
    if (k == 1) { uint32_t g = C8(c[0]); return (g << 16) | (g << 8) | g; }
    if (k == 3) return (C8(c[0]) << 16) | (C8(c[1]) << 8) | C8(c[2]);
    if (k == 4) { fx kk = FX1 - c[3]; return (C8(fxm(FX1 - c[0], kk)) << 16) | (C8(fxm(FX1 - c[1], kk)) << 8) | C8(fxm(FX1 - c[2], kk)); }
    #undef C8
    return 0;
}
static uint32_t *decode_image(struct rctx *R, struct pv *im, int *ow, int *oh, uint32_t mask_color, int depth);
static void run_content(struct rctx *R, const uint8_t *d, uint32_t n, struct pv *res, int depth);
static struct pv *res_get(struct rctx *R, struct pv *res, const char *cat, const struct pv *name)
{
    struct pv *c = dgetr(R->P, res, cat); if (!c || !name || name->t != V_NAME) return 0;
    for (int i = 0; i < c->n && c->t == V_DICT; i++) { struct pv *k = c->a[2 * i]; if (k->n == name->n && !memcmp(k->s, name->s, (size_t)name->n)) return resolve(R->P, c->a[2 * i + 1]); }
    return 0;
}
static void do_xobject(struct rctx *R, struct pv *res, struct pv *name, int depth)
{
    struct pv *x = res_get(R, res, "XObject", name);
    if (!x || x->t != V_DICT || !x->sd) return;
    struct pv *st = dgetr(R->P, x, "Subtype");
    struct gst *g = &R->gs[R->gsp];
    if (name_is(st, "Image")) {
        uint32_t *px = 0; int w = 0, h = 0;
        for (int i = 0; i < R->nic; i++) if (R->ic[i].key == x) { px = R->ic[i].px; w = R->ic[i].w; h = R->ic[i].h; break; }
        if (!px) {
            px = decode_image(R, x, &w, &h, g->fillc, 0);
            if (px && R->nic < 64) { R->ic[R->nic].key = x; R->ic[R->nic].px = px; R->ic[R->nic].w = w; R->ic[R->nic].h = h; R->nic++; }
        }
        if (!px) { R->pg->unknown++; return; }
        fx X[4], Y[4]; xform(g->ctm, 0, 0, &X[0], &Y[0]); xform(g->ctm, FX1, 0, &X[1], &Y[1]); xform(g->ctm, 0, FX1, &X[2], &Y[2]); xform(g->ctm, FX1, FX1, &X[3], &Y[3]);
        fx x0 = X[0], x1 = X[0], y0 = Y[0], y1 = Y[0];
        for (int i = 1; i < 4; i++) { if (X[i] < x0) x0 = X[i]; if (X[i] > x1) x1 = X[i]; if (Y[i] < y0) y0 = Y[i]; if (Y[i] > y1) y1 = Y[i]; }
        struct pdf_op *o = new_op(R, PDF_OP_IMAGE); if (!o) return;
        o->x = (int32_t)(x0 >> 16); o->y = (int32_t)(y0 >> 16); o->w = (int32_t)((x1 - x0) >> 16) + 1; o->h = (int32_t)((y1 - y0) >> 16) + 1;
        o->img = px; o->iw = w; o->ih = h;
        o->flip_x = g->ctm[0] < 0; o->flip_y = Y[2] > Y[0];      /* la fila 0 de la imagen va arriba (unidad y = 1) */
    } else if (name_is(st, "Form") && depth < 12) {
        uint8_t *d; uint32_t n; int jp;
        if (decode_stream(R->P, x, &d, &n, &jp) || jp) { R->pg->unknown++; return; }
        if (R->gsp < 32) { R->gs[R->gsp + 1] = R->gs[R->gsp]; R->gsp++; }
        struct pv *m = dgetr(R->P, x, "Matrix");
        if (m && m->t == V_ARR && m->n >= 6) { fx mm[6]; for (int i = 0; i < 6; i++) { struct pv *e = resolve(R->P, m->a[i]); mm[i] = e && e->t == V_NUM ? e->v : 0; } mat_mul(mm, R->gs[R->gsp].ctm, R->gs[R->gsp].ctm); }
        struct pv *fr = dgetr(R->P, x, "Resources");
        fx tm[6], tlm[6]; memcpy(tm, R->tm, sizeof tm); memcpy(tlm, R->tlm, sizeof tlm);
        run_content(R, d, n, fr && fr->t == V_DICT ? fr : res, depth + 1);
        memcpy(R->tm, tm, sizeof tm); memcpy(R->tlm, tlm, sizeof tlm);
        if (R->gsp > 0) R->gsp--;
        kfree(d);
    }
}
static uint32_t *decode_image(struct rctx *R, struct pv *im, int *ow, int *oh, uint32_t mask_color, int depth)
{
    struct pdf *P = R->P;
    int w = (int)int_of(P, dget(im, "Width"), 0), h = (int)int_of(P, dget(im, "Height"), 0);
    if (w <= 0 || h <= 0 || (int64_t)w * h > (24 << 20)) return 0;
    uint8_t *d; uint32_t n; int jp;
    int dr = decode_stream(P, im, &d, &n, &jp);
    if (dr) return 0;
    uint32_t *px = 0;
    if (jp == 1) {
        struct wimg wi; memset(&wi, 0, sizeof wi);
        if (wi_decode(d, n, &wi) == 0 && wi.px) { px = wi.px; w = wi.w; h = wi.h; }
        kfree(d);
        if (!px) return 0;
    } else if (jp == 2) { kfree(d); return 0; }
    else {
        int bpc = (int)int_of(P, dget(im, "BitsPerComponent"), 8);
        struct pv *msk = dgetr(P, im, "ImageMask"); int is_mask = msk && msk->t == V_BOOL && msk->v;
        if (is_mask) bpc = 1;
        struct pv *cs = dgetr(P, im, "ColorSpace"); int nc = 1; struct pv *pal = 0; int hival = 0; int base_nc = 3; int cmyk = 0;
        if (cs && cs->t == V_ARR && cs->n >= 1) {
            struct pv *k = resolve(P, cs->a[0]);
            if (name_is(k, "Indexed") || name_is(k, "I")) {
                nc = 1; struct pv *b = cs->n > 1 ? resolve(P, cs->a[1]) : 0;
                if (name_is(b, "DeviceGray") || name_is(b, "G") || name_is(b, "CalGray")) base_nc = 1;
                else if (name_is(b, "DeviceCMYK") || name_is(b, "CMYK")) base_nc = 4;
                else if (b && b->t == V_ARR && b->n > 1 && name_is(resolve(P, b->a[0]), "ICCBased")) { struct pv *ic = resolve(P, b->a[1]); base_nc = (int)int_of(P, dget(ic, "N"), 3); }
                hival = cs->n > 2 ? (int)int_of(P, cs->a[2], 255) : 255;
                pal = cs->n > 3 ? resolve(P, cs->a[3]) : 0;
                if (pal && pal->t == V_DICT && pal->sd) { uint8_t *pd; uint32_t pn; int pj; if (!decode_stream(P, pal, &pd, &pn, &pj) && !pj) { struct pv *s2 = (struct pv *)ar_zalloc(R->ar, sizeof *s2); if (s2 && !ar_own(R->ar, pd)) { s2->t = V_STR; s2->s = (const char *)pd; s2->n = (int32_t)pn; pal = s2; } } else pal = 0; }
            } else if (name_is(k, "ICCBased")) { struct pv *ic = cs->n > 1 ? resolve(P, cs->a[1]) : 0; nc = (int)int_of(P, dget(ic, "N"), 3); }
            else if (name_is(k, "CalRGB") || name_is(k, "Lab")) nc = 3;
            else if (name_is(k, "CalGray")) nc = 1;
            else if (name_is(k, "Separation")) nc = 1;
            else if (name_is(k, "DeviceN")) { struct pv *nm = cs->n > 1 ? resolve(P, cs->a[1]) : 0; nc = nm && nm->t == V_ARR ? nm->n : 1; }
        } else if (name_is(cs, "DeviceRGB") || name_is(cs, "RGB")) nc = 3;
        else if (name_is(cs, "DeviceCMYK") || name_is(cs, "CMYK")) nc = 4;
        else nc = 1;
        if (nc == 4 && !pal) cmyk = 1;
        if (nc < 1 || nc > 8 || (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16)) { kfree(d); return 0; }
        uint32_t row = (uint32_t)(((int64_t)w * nc * bpc + 7) / 8);
        if ((uint64_t)row * h > n) { if (n < row) { kfree(d); return 0; } h = (int)(n / row); }
        px = (uint32_t *)kmalloc((size_t)w * h * 4); if (!px) { kfree(d); return 0; }
        struct pv *dec = dgetr(P, im, "Decode"); int inv = 0;
        if (dec && dec->t == V_ARR && dec->n >= 2 && dec->a[0]->t == V_NUM && dec->a[0]->v > dec->a[1]->v) inv = 1;
        int maxv = (1 << (bpc > 8 ? 8 : bpc)) - 1;
        for (int y = 0; y < h; y++) {
            const uint8_t *r = d + (size_t)y * row;
            for (int x = 0; x < w; x++) {
                int c[8];
                for (int k = 0; k < nc; k++) {
                    int idx = x * nc + k, v;
                    if (bpc == 8) v = r[idx];
                    else if (bpc == 16) v = r[idx * 2];
                    else { int bit = idx * bpc; v = (r[bit >> 3] >> (8 - bpc - (bit & 7))) & maxv; }
                    c[k] = v;
                }
                uint32_t out;
                if (is_mask) { int on = (c[0] == 0) != inv; out = on ? (0xFF000000u | mask_color) : 0; }
                else if (pal) {
                    int i = c[0] > hival ? hival : c[0]; const uint8_t *pp = (const uint8_t *)pal->s; int off = i * base_nc;
                    if (off + base_nc > pal->n) out = 0xFF000000u;
                    else if (base_nc == 1) out = 0xFF000000u | ((uint32_t)pp[off] * 0x010101u);
                    else if (base_nc == 4) { int kk = 255 - pp[off + 3]; out = 0xFF000000u | ((uint32_t)((255 - pp[off]) * kk / 255) << 16) | ((uint32_t)((255 - pp[off + 1]) * kk / 255) << 8) | (uint32_t)((255 - pp[off + 2]) * kk / 255); }
                    else out = 0xFF000000u | ((uint32_t)pp[off] << 16) | ((uint32_t)pp[off + 1] << 8) | pp[off + 2];
                } else {
                    int sc = bpc >= 8 ? 1 : 255 / maxv;
                    for (int k = 0; k < nc; k++) { c[k] *= sc; if (inv) c[k] = 255 - c[k]; }
                    if (nc == 1 || nc == 2) out = 0xFF000000u | ((uint32_t)c[0] * 0x010101u);
                    else if (cmyk) { int kk = 255 - c[3]; out = 0xFF000000u | ((uint32_t)((255 - c[0]) * kk / 255) << 16) | ((uint32_t)((255 - c[1]) * kk / 255) << 8) | (uint32_t)((255 - c[2]) * kk / 255); }
                    else out = 0xFF000000u | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
                }
                px[(size_t)y * w + x] = out;
            }
        }
        kfree(d);
    }
    /* mascara suave (transparencia) */
    struct pv *sm = dgetr(P, im, "SMask");
    if (sm && sm->t == V_DICT && sm->sd && depth == 0) {
        int mw, mh; uint32_t *m = decode_image(R, sm, &mw, &mh, 0xFFFFFF, 1);
        if (m) {
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                int sx = (int)((int64_t)x * mw / w), sy = (int)((int64_t)y * mh / h);
                uint32_t a = m[(size_t)sy * mw + sx] & 255; px[(size_t)y * w + x] = (px[(size_t)y * w + x] & 0xFFFFFF) | (a << 24);
            }
            kfree(m);
        }
    }
    if (depth == 0 && ar_own(R->ar, px)) return 0;
    *ow = w; *oh = h;
    return px;
}
static int op_is(const struct pv *o, const char *s) { int n = (int)wu_strlen(s); return o->n == n && !memcmp(o->s, s, (size_t)n); }
static struct font *get_font(struct rctx *R, struct pv *res, struct pv *name)
{
    struct pv *fd = res_get(R, res, "Font", name);
    if (!fd || fd->t != V_DICT) return 0;
    for (int i = 0; i < R->nfc; i++) if (R->fc[i].key == fd) return R->fc[i].f;
    struct font *f = load_font(R->P, fd, R->ar);
    if (f && R->nfc < 64) { R->fc[R->nfc].key = fd; R->fc[R->nfc].f = f; R->nfc++; }
    return f;
}
static void run_content(struct rctx *R, const uint8_t *d, uint32_t n, struct pv *res, int depth)
{
    struct arena scr0 = R->scr; memset(&R->scr, 0, sizeof R->scr);   /* operandos en una arena aparte que se vacia en cada operador */
    struct lex L = { d, d + n, R->P, &R->scr, 1 };
    struct pv *st[48]; int ns = 0;
    int base_gsp = R->gsp;
    for (uint32_t guard = 0; guard < 4000000 && L.p < L.e; guard++) {
        /* arena por bloques: los operandos de cada operador no se guardan */
        struct pv *v = parse_val(&L, 0);
        if (!v) { if (L.p >= L.e) break; continue; }
        if (v->t != V_OP) { if (ns < 48) st[ns++] = v; continue; }
        struct gst *g = &R->gs[R->gsp];
        #define N(i) (ns > (i) && st[i]->t == V_NUM ? st[i]->v : 0)
        const char *o = v->s; int on = v->n;
        if (on == 1) switch (o[0]) {
            case 'q': if (R->gsp < 32) { R->gs[R->gsp + 1] = R->gs[R->gsp]; R->gsp++; } break;
            case 'Q': if (R->gsp > base_gsp) R->gsp--; break;
            case 'w': g->lw = N(0); break;
            case 'g': if (ns >= 1) { g->fillc = rgb_of(st, 1, ns - 1); g->fill_pat = 0; } break;
            case 'G': if (ns >= 1) g->strokec = rgb_of(st, 1, ns - 1); break;
            case 'k': if (ns >= 4) { g->fillc = rgb_of(st, ns, ns - 4); g->fill_pat = 0; } break;
            case 'K': if (ns >= 4) g->strokec = rgb_of(st, ns, ns - 4); break;
            case 'm': if (ns >= 2) { path_break(R); R->cx = R->sx = N(ns - 2); R->cy = R->sy = N(ns - 1); path_pt(R, R->cx, R->cy); R->has_cur = 1; } break;
            case 'l': if (ns >= 2) { if (!R->has_cur) { path_break(R); R->sx = N(ns - 2); R->sy = N(ns - 1); } R->cx = N(ns - 2); R->cy = N(ns - 1); path_pt(R, R->cx, R->cy); R->has_cur = 1; } break;
            case 'c': if (ns >= 6) curve(R, N(0), N(1), N(2), N(3), N(4), N(5)); break;
            case 'v': if (ns >= 4) curve(R, R->cx, R->cy, N(0), N(1), N(2), N(3)); break;
            case 'y': if (ns >= 4) curve(R, N(0), N(1), N(2), N(3), N(2), N(3)); break;
            case 'h': if (R->has_cur) { path_pt(R, R->sx, R->sy); R->cx = R->sx; R->cy = R->sy; } break;
            case 'f': case 'F': paint(R, 1, 0, 0); break;
            case 'S': paint(R, 0, 1, 0); break;
            case 's': if (R->has_cur) path_pt(R, R->sx, R->sy); paint(R, 0, 1, 0); break;
            case 'B': paint(R, 1, 1, 0); break;
            case 'b': if (R->has_cur) path_pt(R, R->sx, R->sy); paint(R, 1, 1, 0); break;
            case 'n': R->np = 0; R->has_cur = 0; break;
            case 'W': break;
            case '\'': memcpy(R->tm, R->tlm, sizeof R->tm); R->tm[4] += fxm(-g->tl, R->tm[2]); R->tm[5] += fxm(-g->tl, R->tm[3]); memcpy(R->tlm, R->tm, sizeof R->tm); flush_word(R); if (ns >= 1) show_text(R, st[ns - 1]); break;
            case '"': if (ns >= 3) { g->tw = N(0); g->tc = N(1); } memcpy(R->tm, R->tlm, sizeof R->tm); R->tm[4] += fxm(-g->tl, R->tm[2]); R->tm[5] += fxm(-g->tl, R->tm[3]); memcpy(R->tlm, R->tm, sizeof R->tm); flush_word(R); if (ns >= 1) show_text(R, st[ns - 1]); break;
            default: break;
        } else if (on == 2) {
            if (op_is(v, "cm") && ns >= 6) { fx m[6]; for (int i = 0; i < 6; i++) m[i] = N(i); mat_mul(m, g->ctm, g->ctm); }
            else if (op_is(v, "rg") && ns >= 3) { g->fillc = rgb_of(st, ns, ns - 3); g->fill_pat = 0; }
            else if (op_is(v, "RG") && ns >= 3) g->strokec = rgb_of(st, ns, ns - 3);
            else if (op_is(v, "re") && ns >= 4) {
                fx x = N(0), y = N(1), w = N(2), h = N(3);
                path_break(R); path_pt(R, x, y); path_pt(R, x + w, y); path_pt(R, x + w, y + h); path_pt(R, x, y + h); path_pt(R, x, y);
                R->cx = R->sx = x; R->cy = R->sy = y; R->has_cur = 0;
            }
            else if (op_is(v, "f*")) paint(R, 1, 0, 1);
            else if (op_is(v, "B*")) paint(R, 1, 1, 1);
            else if (op_is(v, "b*")) { if (R->has_cur) path_pt(R, R->sx, R->sy); paint(R, 1, 1, 1); }
            else if (op_is(v, "W*")) { }
            else if (op_is(v, "BT")) { R->tm[0] = R->tm[3] = FX1; R->tm[1] = R->tm[2] = R->tm[4] = R->tm[5] = 0; memcpy(R->tlm, R->tm, sizeof R->tm); }
            else if (op_is(v, "ET")) flush_word(R);
            else if (op_is(v, "Tf") && ns >= 2) { g->font = get_font(R, res, st[ns - 2]); g->fs = N(ns - 1); }
            else if (op_is(v, "Tc")) g->tc = N(0);
            else if (op_is(v, "Tw")) g->tw = N(0);
            else if (op_is(v, "Tz")) g->th = N(0) / 100;
            else if (op_is(v, "TL")) g->tl = N(0);
            else if (op_is(v, "Ts")) g->rise = N(0);
            else if (op_is(v, "Tr")) g->tr = (int)(N(0) >> 16);
            else if ((op_is(v, "Td") || op_is(v, "TD")) && ns >= 2) {
                fx tx = N(0), ty = N(1);
                if (o[1] == 'D') g->tl = -ty;
                fx m[6] = { FX1, 0, 0, FX1, tx, ty }; mat_mul(m, R->tlm, R->tlm); memcpy(R->tm, R->tlm, sizeof R->tm);
            }
            else if (op_is(v, "Tm") && ns >= 6) { for (int i = 0; i < 6; i++) R->tm[i] = N(i); memcpy(R->tlm, R->tm, sizeof R->tm); flush_word(R); }
            else if (op_is(v, "T*")) { fx m[6] = { FX1, 0, 0, FX1, 0, -g->tl }; mat_mul(m, R->tlm, R->tlm); memcpy(R->tm, R->tlm, sizeof R->tm); }
            else if (op_is(v, "Tj") && ns >= 1) show_text(R, st[ns - 1]);
            else if (op_is(v, "TJ") && ns >= 1 && st[ns - 1]->t == V_ARR) {
                struct pv *a = st[ns - 1];
                for (int i = 0; i < a->n; i++) { if (a->a[i]->t == V_STR) show_text(R, a->a[i]); else if (a->a[i]->t == V_NUM) tj_adjust(R, a->a[i]->v); }
            }
            else if (op_is(v, "Do") && ns >= 1) { flush_word(R); do_xobject(R, res, st[ns - 1], depth); }
            else if (op_is(v, "cs") || op_is(v, "CS")) { if (o[0] == 'c') { g->fill_pat = ns >= 1 && name_is(st[0], "Pattern"); if (!g->fill_pat) g->fillc = 0; } else g->strokec = 0; }
            else if (op_is(v, "sc")) { if (ns >= 1) g->fillc = rgb_of(st, ns, ns >= 4 ? ns - 4 : ns >= 3 ? ns - 3 : ns - 1); }
            else if (op_is(v, "SC")) { if (ns >= 1) g->strokec = rgb_of(st, ns, ns >= 4 ? ns - 4 : ns >= 3 ? ns - 3 : ns - 1); }
            else if (op_is(v, "BI")) {   /* imagen en linea: se salta */
                const uint8_t *p = L.p; while (p + 2 < L.e && !(p[0] == 'I' && p[1] == 'D' && is_ws(p[2]))) p++;
                p += 3; while (p + 2 < L.e && !(is_ws(p[-1]) && p[0] == 'E' && p[1] == 'I' && (p + 2 >= L.e || is_ws(p[2])))) p++;
                L.p = p + 2 <= L.e ? p + 2 : L.e; R->pg->unknown++;
            }
            else if (op_is(v, "gs") || op_is(v, "ri") || op_is(v, "sh") || op_is(v, "MP") || op_is(v, "DP") || op_is(v, "BX") || op_is(v, "EX") || op_is(v, "d0") || op_is(v, "d1")) { }
            else R->pg->unknown++;
        } else if (on == 3) {
            if (op_is(v, "scn")) { if (ns >= 1 && st[ns - 1]->t == V_NAME) g->fill_pat = 1; else if (ns >= 1) { g->fillc = rgb_of(st, ns, ns >= 4 ? ns - 4 : ns >= 3 ? ns - 3 : ns - 1); g->fill_pat = 0; } }
            else if (op_is(v, "SCN")) { if (ns >= 1 && st[ns - 1]->t == V_NUM) g->strokec = rgb_of(st, ns, ns >= 4 ? ns - 4 : ns >= 3 ? ns - 3 : ns - 1); }
            else if (op_is(v, "BDC") || op_is(v, "BMC") || op_is(v, "EMC")) { }
            else R->pg->unknown++;
        } else if (on == 1 && o[0] == 'd') { }
        else if (op_is(v, "d") || op_is(v, "i") || op_is(v, "j") || op_is(v, "J") || op_is(v, "M")) { }
        else R->pg->unknown++;
        #undef N
        ns = 0;
        ar_reset(&R->scr);
        if (!R->nops_budget) break;
    }
    flush_word(R);
    ar_free(&R->scr); R->scr = scr0;
    R->gsp = base_gsp;
}

int pdf_render_page(struct pdf *P, int idx, int scale256, struct pdf_page *out)
{
    memset(out, 0, sizeof *out);
    if (!P || idx < 0 || idx >= P->npages) return -1;
    if (scale256 < 16) scale256 = 16;
    if (scale256 > 2048) scale256 = 2048;
    struct pgrec *pg = &P->pages[idx];
    struct arena *ar = (struct arena *)kmalloc(sizeof *ar); if (!ar) return -1;
    memset(ar, 0, sizeof *ar); out->arena = ar;
    struct rctx *R = (struct rctx *)kmalloc(sizeof *R); if (!R) { pdf_page_free(out); return -1; }
    memset(R, 0, sizeof *R);
    R->P = P; R->pg = out; R->ar = ar; R->nops_budget = 400000;
    fx s = (fx)scale256 << 8;
    fx W = pg->box[2] - pg->box[0], H = pg->box[3] - pg->box[1];
    fx base[6];
    switch (pg->rot) {
    case 90:  base[0] = 0; base[1] = s; base[2] = s; base[3] = 0; base[4] = -fxm(pg->box[1], s); base[5] = -fxm(pg->box[0], s); out->w = (int)(fxm(H, s) >> 16); out->h = (int)(fxm(W, s) >> 16); break;
    case 180: base[0] = -s; base[1] = 0; base[2] = 0; base[3] = s; base[4] = fxm(pg->box[2], s); base[5] = -fxm(pg->box[1], s); out->w = (int)(fxm(W, s) >> 16); out->h = (int)(fxm(H, s) >> 16); break;
    case 270: base[0] = 0; base[1] = -s; base[2] = -s; base[3] = 0; base[4] = fxm(pg->box[3], s); base[5] = fxm(pg->box[2], s); out->w = (int)(fxm(H, s) >> 16); out->h = (int)(fxm(W, s) >> 16); break;
    default:  base[0] = s; base[1] = 0; base[2] = 0; base[3] = -s; base[4] = -fxm(pg->box[0], s); base[5] = fxm(pg->box[3], s); out->w = (int)(fxm(W, s) >> 16); out->h = (int)(fxm(H, s) >> 16); break;
    }
    struct gst *g = &R->gs[0];
    memcpy(g->ctm, base, sizeof base); g->th = FX1; g->lw = FX1; g->fillc = 0; g->strokec = 0;
    struct pv *c = dgetr(P, pg->dict, "Contents");
    struct pv *one[1]; struct pv **list = 0; int nl = 0;
    if (c && c->t == V_DICT) { one[0] = c; list = one; nl = 1; }
    else if (c && c->t == V_ARR) { list = c->a; nl = c->n; }
    /* los trozos de contenido se concatenan (un operador puede quedar partido entre dos) */
    struct wb all; memset(&all, 0, sizeof all);
    for (int i = 0; i < nl; i++) {
        struct pv *s1 = resolve(P, list[i]); uint8_t *d; uint32_t n; int jp;
        if (!s1 || decode_stream(P, s1, &d, &n, &jp) || jp) { out->unknown++; continue; }
        wb_addn(&all, (const char *)d, n); wb_addn(&all, "\n", 1); kfree(d);
    }
    if (all.p) run_content(R, (const uint8_t *)all.p, all.len, pg->res, 0);
    wb_free(&all);
    if (R->path) kfree(R->path);
    kfree(R);
    return 0;
}
void pdf_page_free(struct pdf_page *pg)
{
    if (!pg) return;
    if (pg->ops) kfree(pg->ops);
    if (pg->arena) { ar_free((struct arena *)pg->arena); kfree(pg->arena); }
    memset(pg, 0, sizeof *pg);
}
int pdf_page_text(const struct pdf_page *pg, char *out, int max)
{
    int k = 0, lasty = -100000, lastx = 0;
    if (max < 1) return 0;
    for (int i = 0; i < pg->nops && k < max - 2; i++) {
        const struct pdf_op *o = &pg->ops[i];
        if (o->kind != PDF_OP_TEXT || !o->text) continue;
        int dy = o->y - lasty; if (dy < 0) dy = -dy;
        if (k > 0) { if (dy > o->size / 2) out[k++] = '\n'; else if (o->x > lastx) out[k++] = ' '; }
        for (int j = 0; j < o->len && k < max - 1; j++) out[k++] = o->text[j];
        lasty = o->y; lastx = o->x + o->w;
    }
    out[k] = 0; return k;
}
