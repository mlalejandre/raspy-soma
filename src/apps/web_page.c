/* SOMA_PARCHE_NAVEGADOR: pagina web viva (red, HTML, CSS, scripts, maquetacion, imagenes, formularios, navegacion, interaccion). */
#include "web_page.h"
#include "web_ua.h"
#include "web_img.h"
#include "web_font.h"

#define WP_UA "Mozilla/5.0 (X11; SOMA) AppleWebKit/537.36 (KHTML, like Gecko) SOMA-Browser/2.0 Safari/537.36"
#define WP_MAX_HTML (3u << 20)
#define WP_MAX_RES (2u << 20)
#define WP_IMG_BUDGET (20u << 20)
#define WP_IMG_STORE_PIX (1024u * 768u)     /* SOMA_PARCHE_028_UI: una imagen se guarda con hasta ~0,8 Mpx (la pagina rara vez la muestra mayor) */
#define WP_IMG_MAX_PIXELS (2200u * 1600u)
#define WP_MAX_IMGS 160
#define WP_LOAD_BUDGET_MS 40000u
#define WP_MAX_SVG 96
#define WP_SVG_MAX_BYTES (48u * 1024u)
struct wp_svg { uint32_t hash; char *markup; uint32_t len; uint32_t color; };

__attribute__((weak)) int wp_fs_read(const char *path, char **out, uint32_t *len) { (void)path; *out = 0; *len = 0; return -1; }
__attribute__((weak)) void wp_log(int level, const char *msg) { (void)level; (void)msg; }

void wd_decode_entities(const char *s, uint32_t n, struct wb *out);

static uint64_t now_ms(void) { return web_now_ms(); }

/* ------------------------------------------------------------ almacenamiento web (localStorage persistente en memoria, sessionStorage por pagina) */
struct wp_store { char *k[160]; char *v[160]; int n; uint32_t bytes; };
static struct wp_store g_local;

static char *sdup(const char *s) { uint32_t n = wu_strlen(s); char *p = (char *)kmalloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
static void store_clear(struct wp_store *s) { for (int i = 0; i < s->n; i++) { kfree(s->k[i]); kfree(s->v[i]); } s->n = 0; s->bytes = 0; }

static const char *store_op(struct wp_store *s, const char *origin, const char *key, const char *val, int op)
{
    char full[200]; uint32_t ol = wu_strlen(origin); if (ol > 120) ol = 120;
    memcpy(full, origin, ol); full[ol++] = 1;
    if (op == 3) {                                  /* clear: solo las claves de este origen */
        for (int i = s->n - 1; i >= 0; i--) if (wu_starts(s->k[i], full) ) { s->bytes -= wu_strlen(s->k[i]) + wu_strlen(s->v[i]); kfree(s->k[i]); kfree(s->v[i]); s->n--; s->k[i] = s->k[s->n]; s->v[i] = s->v[s->n]; }
        return 0;
    }
    if (op == 4) {                                  /* key(i) */
        int want = wu_atoi(key), c = 0;
        for (int i = 0; i < s->n; i++) if (wu_starts(s->k[i], full)) { if (c++ == want) return s->k[i] + ol; }
        return 0;
    }
    wu_copy(full + ol, key, sizeof full - ol);
    int idx = -1; for (int i = 0; i < s->n; i++) if (wu_streq(s->k[i], full)) { idx = i; break; }
    if (op == 0) return idx >= 0 ? s->v[idx] : 0;
    if (op == 2) { if (idx >= 0) { s->bytes -= wu_strlen(s->k[idx]) + wu_strlen(s->v[idx]); kfree(s->k[idx]); kfree(s->v[idx]); s->n--; s->k[idx] = s->k[s->n]; s->v[idx] = s->v[s->n]; } return 0; }
    /* set */
    uint32_t vl = wu_strlen(val);
    if (vl > (256u << 10)) return 0;
    if (idx >= 0) { char *nv = sdup(val); if (!nv) return 0; s->bytes = s->bytes - wu_strlen(s->v[idx]) + vl; kfree(s->v[idx]); s->v[idx] = nv; return 0; }
    if (s->n >= 160 || s->bytes + vl > (2u << 20)) return 0;
    char *nk = sdup(full), *nv = sdup(val); if (!nk || !nv) { if (nk) kfree(nk); if (nv) kfree(nv); return 0; }
    s->k[s->n] = nk; s->v[s->n] = nv; s->n++; s->bytes += wu_strlen(nk) + vl;
    return 0;
}

/* ------------------------------------------------------------ URL / texto */
static int is_scheme_char(int c) { return wu_isalnum(c) || c == '+' || c == '-' || c == '.'; }
static int has_scheme(const char *u)
{
    const char *p = u; if (!wu_isalpha(*p)) return 0;
    while (is_scheme_char(*p)) p++;
    if (*p != ':') return 0;
    if (p - u == 1) return 0;                                         /* "c:" */
    const char *q = p + 1;                                            /* host:puerto no es un esquema */
    if (wu_isdigit(*q)) { while (wu_isdigit(*q)) q++; if (*q == 0 || *q == '/' || *q == '?' || *q == '#') return 0; }
    return 1;
}
static void pct_enc(struct wb *o, const char *s, int plus_space)
{
    static const char hx[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (wu_isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '*') wb_addc(o, (char)c);
        else if (c == ' ' && plus_space) wb_addc(o, '+');
        else { wb_addc(o, '%'); wb_addc(o, hx[c >> 4]); wb_addc(o, hx[c & 15]); }
    }
}
/* Normaliza lo escrito en la barra de direcciones. *implicit = 1 si se anadio el esquema. */
static void norm_url(const char *in, char *out, uint32_t max, int *implicit)
{
    while (*in == ' ' || *in == '\t') in++;
    uint32_t n = wu_strlen(in); while (n && (in[n - 1] == ' ' || in[n - 1] == '\t' || in[n - 1] == '\n' || in[n - 1] == '\r')) n--;
    char t[WN_URL_MAX]; wu_copyn(t, in, n, sizeof t);
    *implicit = 0;
    if (!t[0]) { out[0] = 0; return; }
    if (has_scheme(t)) { wu_copy(out, t, max); return; }
    if (t[0] == '/' && t[1] != '/') { wu_copy(out, "file://", max); uint32_t l = wu_strlen(out); wu_copy(out + l, t, max - l); return; }
    if (t[0] == '/' && t[1] == '/') { wu_copy(out, "https:", max); uint32_t l = wu_strlen(out); wu_copy(out + l, t, max - l); return; }
    if (t[0] == '#' || t[0] == '?') { wu_copy(out, t, max); return; }
    int dot = 0, sp = 0; for (const char *q = t; *q && *q != '/'; q++) if (*q == '.') dot = 1; for (const char *q = t; *q; q++) if (*q == ' ') sp = 1;
    int local = wu_startsi(t, "localhost") || wu_startsi(t, "10.") || wu_startsi(t, "192.168.") || wu_startsi(t, "127.");
    if (!sp && (dot || local || wu_find(t, ":"))) {
        const char *pre = local || wu_find(t, ":") ? "http://" : "https://";
        struct wb b = {0}; wb_add(&b, pre);
        for (const char *q = t; *q; q++) { if ((unsigned char)*q > 126 || *q == ' ') { char e[4] = { '%', "0123456789ABCDEF"[(*q >> 4) & 15], "0123456789ABCDEF"[*q & 15], 0 }; wb_add(&b, e); } else wb_addc(&b, *q); }
        wu_copy(out, b.p ? b.p : "", max); wb_free(&b); *implicit = 1; return;
    }
    /* texto libre: busqueda (version HTML sin JavaScript) */
    struct wb b = {0}; wb_add(&b, "https://html.duckduckgo.com/html/?q="); pct_enc(&b, t, 1);
    wu_copy(out, b.p ? b.p : "", max); wb_free(&b);
}
static void html_escape(struct wb *o, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '<') wb_add(o, "&lt;"); else if (c == '>') wb_add(o, "&gt;"); else if (c == '&') wb_add(o, "&amp;"); else if (c == '"') wb_add(o, "&quot;"); else wb_addc(o, c);
    }
}
static void set_status(struct wpage *p, int kind, const char *msg) { p->status_kind = kind; wu_copy(p->status, msg, sizeof p->status); }
static void abs_url(struct wpage *p, const char *ref, char *out, uint32_t max)
{
    if (!ref) { out[0] = 0; return; }
    while (wu_isspace(*ref)) ref++;
    if (p->doc && p->doc->base[0]) { if (wn_url_resolve(p->doc->base, ref, out, max) == 0) return; }
    else if (p->url[0] && wn_url_resolve(p->url, ref, out, max) == 0) return;
    wu_copy(out, ref, max);
}
static int ext_is(const char *path, const char *ext)
{
    uint32_t n = wu_strlen(path), e = wu_strlen(ext);
    const char *q = wu_strchr(path, '?'); if (q) n = (uint32_t)(q - path);
    const char *h = wu_strchr(path, '#'); if (h && (uint32_t)(h - path) < n) n = (uint32_t)(h - path);
    return n >= e && wu_strnieq(path + n - e, ext, e);
}
static const char *guess_ctype(const char *path)
{
    if (ext_is(path, ".html") || ext_is(path, ".htm") || ext_is(path, ".xhtml")) return "text/html";
    if (ext_is(path, ".css")) return "text/css";
    if (ext_is(path, ".js") || ext_is(path, ".mjs")) return "application/javascript";
    if (ext_is(path, ".json")) return "application/json";
    if (ext_is(path, ".png")) return "image/png";
    if (ext_is(path, ".jpg") || ext_is(path, ".jpeg")) return "image/jpeg";
    if (ext_is(path, ".gif")) return "image/gif";
    if (ext_is(path, ".bmp")) return "image/bmp";
    if (ext_is(path, ".svg")) return "image/svg+xml";
    if (ext_is(path, ".txt") || ext_is(path, ".md") || ext_is(path, ".c") || ext_is(path, ".h") || ext_is(path, ".soma")) return "text/plain";
    return "";
}

/* ------------------------------------------------------------ descarga de recursos (http, https, file, data) */
static int b64v(int c) { if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26; if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+' || c == '-') return 62; if (c == '/' || c == '_') return 63; return -1; }
static int load_data_url(const char *url, struct wn_resp *rs)
{
    const char *p = url + 5; const char *comma = wu_strchr(p, ',');
    if (!comma) return -1;
    int b64 = 0; char meta[100]; wu_copyn(meta, p, (uint32_t)(comma - p), sizeof meta);
    if (wu_findi(meta, ";base64")) b64 = 1;
    char *semi = (char *)wu_strchr(meta, ';'); if (semi) *semi = 0;
    wu_copy(rs->ctype, meta[0] ? meta : "text/plain", sizeof rs->ctype);
    const char *cs = wu_findi(comma - (comma - p) , "charset="); (void)cs;
    uint32_t n = wu_strlen(comma + 1); char *o = (char *)kmalloc(n + 1); if (!o) return -1; uint32_t k = 0;
    const char *s = comma + 1;
    if (b64) {
        uint32_t acc = 0; int bits = 0;
        for (; *s; s++) { int v = b64v((unsigned char)*s); if (*s == '%' && wu_ishex(s[1]) && wu_ishex(s[2])) { s += 2; continue; } if (v < 0) continue; acc = (acc << 6) | (uint32_t)v; bits += 6; if (bits >= 8) { bits -= 8; o[k++] = (char)((acc >> bits) & 255); } }
    } else {
        for (; *s; s++) { if (*s == '%' && wu_ishex(s[1]) && wu_ishex(s[2])) { o[k++] = (char)(wu_hexval(s[1]) * 16 + wu_hexval(s[2])); s += 2; } else o[k++] = *s; }
    }
    o[k] = 0; rs->body = o; rs->len = k; rs->status = 200; wu_copy(rs->final_url, url, 64); return 0;
}
static int load_file_url(const char *url, struct wn_resp *rs)
{
    const char *p = url; if (wu_startsi(p, "file:")) p += 5;
    while (p[0] == '/' && p[1] == '/') p++;
    char path[300]; wu_copy(path, p[0] == '/' ? p : p - 0, sizeof path);
    if (path[0] != '/') { char t[300]; wu_copy(t, "/", sizeof t); wu_copy(t + 1, path, sizeof t - 1); wu_copy(path, t, sizeof path); }
    uint32_t pn = wu_strlen(path); for (uint32_t i = 0; i < pn; i++) if (path[i] == '?' || path[i] == '#') { path[i] = 0; break; }
    char *buf = 0; uint32_t len = 0;
    if (wp_fs_read(path, &buf, &len) != 0 || !buf) { wu_copy(rs->err, "archivo no encontrado", sizeof rs->err); rs->status = 404; return -1; }
    rs->body = buf; rs->len = len; rs->status = 200; wu_copy(rs->ctype, guess_ctype(path), sizeof rs->ctype); wu_copy(rs->final_url, url, sizeof rs->final_url);
    return 0;
}
static int load_res(struct wpage *p, const char *url, const char *method, const char *body, uint32_t blen, const char *ctype, int binary, uint32_t maxb, uint32_t tmo, struct wn_resp *rs)
{
    memset(rs, 0, sizeof *rs);
    if (wu_startsi(url, "data:")) return load_data_url(url, rs);
    if (wu_startsi(url, "file:") || url[0] == '/') return load_file_url(url, rs);
    if (!wu_startsi(url, "http://") && !wu_startsi(url, "https://")) { wu_copy(rs->err, "protocolo no soportado", sizeof rs->err); return -11; }
    struct wn_req rq; memset(&rq, 0, sizeof rq);
    rq.url = url; rq.method = method; rq.body = body; rq.body_len = blen; rq.ctype = ctype; rq.referer = p->url; rq.timeout_ms = tmo; rq.max_body = maxb; rq.want_binary = binary;
    if (binary) rq.accept = "image/png,image/jpeg,image/gif";
    return wn_fetch(&rq, rs);
}

/* Convierte el cuerpo a UTF-8 segun el juego de caracteres declarado. Devuelve un bloque nuevo (kmalloc) y libera el original. */
static void to_utf8(struct wn_resp *rs, int is_html)
{
    if (!rs->body) return;
    int latin = 0;
    if (rs->charset[0]) latin = wu_startsi(rs->charset, "iso-8859") || wu_startsi(rs->charset, "latin") || wu_startsi(rs->charset, "windows-125") || wu_startsi(rs->charset, "cp125") || wu_streq(rs->charset, "us-ascii");
    else if (is_html) {
        uint32_t n = rs->len < 3000 ? rs->len : 3000; char head[3001]; memcpy(head, rs->body, n); head[n] = 0;
        const char *c = wu_findi(head, "charset=");
        if (c) { c += 8; if (*c == '"' || *c == '\'') c++; latin = wu_startsi(c, "iso-8859") || wu_startsi(c, "latin") || wu_startsi(c, "windows-125") || wu_startsi(c, "cp125"); }
    }
    if (!latin) {
        if (rs->len >= 3 && (uint8_t)rs->body[0] == 0xEF && (uint8_t)rs->body[1] == 0xBB && (uint8_t)rs->body[2] == 0xBF) { memmove(rs->body, rs->body + 3, rs->len - 3); rs->len -= 3; rs->body[rs->len] = 0; }
        return;
    }
    char *o = (char *)kmalloc(rs->len * 2 + 2); if (!o) return;
    uint32_t k = wd_latin1_to_utf8((const uint8_t *)rs->body, rs->len, o, rs->len * 2 + 1); o[k] = 0;
    kfree(rs->body); rs->body = o; rs->len = k;
}

/* ------------------------------------------------------------ cache de imagenes */
static void img_free_all(struct wpage *p)
{
    for (int i = 0; i < p->nimg; i++) { if (p->imgs[i].px) kfree(p->imgs[i].px); if (p->imgs[i].url) kfree(p->imgs[i].url); }
    if (p->imgs) kfree(p->imgs);
    p->imgs = 0; p->nimg = p->capimg = 0; p->img_bytes = 0; p->img_pending = 0;
    for (int i = 0; i < p->nsvg; i++) if (p->svgs[i].markup) kfree(p->svgs[i].markup);
    if (p->svgs) kfree(p->svgs);
    p->svgs = 0; p->nsvg = 0;
}
static struct wp_img *img_find(struct wpage *p, const char *url, uint32_t h)
{
    for (int i = 0; i < p->nimg; i++) if (p->imgs[i].hash == h && wu_streq(p->imgs[i].url, url)) return &p->imgs[i];
    return 0;
}
static struct wp_img *img_add(struct wpage *p, const char *url, uint32_t h)
{
    if (p->nimg >= WP_MAX_IMGS) return 0;
    if (p->nimg == p->capimg) {
        int nc = p->capimg ? p->capimg * 2 : 16; if (nc > WP_MAX_IMGS) nc = WP_MAX_IMGS;
        struct wp_img *n = (struct wp_img *)kmalloc((uint32_t)nc * sizeof *n); if (!n) return 0;
        if (p->imgs) { memcpy(n, p->imgs, (uint32_t)p->nimg * sizeof *n); kfree(p->imgs); }
        memset(n + p->nimg, 0, (uint32_t)(nc - p->nimg) * sizeof *n); p->imgs = n; p->capimg = nc;
    }
    struct wp_img *e = &p->imgs[p->nimg]; memset(e, 0, sizeof *e); e->url = sdup(url); if (!e->url) return 0;
    e->hash = h; p->nimg++; return e;
}
static int img_cb(void *ud, const char *src, struct wimginfo *o)
{
    struct wpage *p = (struct wpage *)ud; char u[WN_URL_MAX];
    if (!src || !*src || wu_startsi(src, "about:") || wu_startsi(src, "javascript:")) { o->state = 3; return 0; }
    abs_url(p, src, u, sizeof u);
    uint32_t h = wu_hash(u, wu_strlen(u)); struct wp_img *e = img_find(p, u, h);
    if (!e) { e = img_add(p, u, h); if (!e) { o->state = 3; return 0; } e->state = 1; p->img_pending++; }
    o->state = e->state;
    if (e->state == 2) { o->w = e->w; o->h = e->h; o->px = e->px; o->has_alpha = e->has_alpha; o->pw = e->pw; o->ph = e->ph; return 1; }
    return 0;
}
static const char *img_kind(const uint8_t *d, uint32_t n)
{
    if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return "WebP (no soportado)";
    if (n >= 12 && !memcmp(d + 4, "ftyp", 4)) return "AVIF/HEIC (no soportado)";
    if (n >= 5 && (!memcmp(d, "<svg", 4) || !memcmp(d, "<?xm", 4))) return "SVG (no soportado)";
    if (n >= 5 && (d[0] == '<' || !memcmp(d, "\xEF\xBB\xBF<", 4))) return "HTML (no es imagen)";
    if (n >= 4 && d[0] == 0x89 && d[1] == 'P') return "PNG no decodificable";
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return "JPEG no decodificable (CMYK, aritmetico o danado)";
    if (n >= 3 && !memcmp(d, "GIF", 3)) return "GIF danado";
    return "formato desconocido";
}
static void img_note_fail(struct wpage *p, const char *url, const char *why, int status, uint32_t len)
{
    p->img_fail++;
    if (!p->img_why[0]) { uint32_t k = 0; for (const char *s = why; *s && k < 30; s++) p->img_why[k++] = *s; p->img_why[k++] = ' '; k += (uint32_t)wu_itoa(status, p->img_why + k); p->img_why[k] = 0; }
    if (p->img_logged >= 24) return;
    p->img_logged++;
    char m[200]; uint32_t k = 0; const char *a = "img KO: ";
    while (*a) m[k++] = *a++;
    for (const char *s = url; *s && k < 92; s++) m[k++] = *s;
    m[k++] = ' '; m[k++] = 'h'; m[k++] = '='; k += (uint32_t)wu_itoa(status, m + k);
    m[k++] = ' '; m[k++] = 'n'; m[k++] = '='; k += (uint32_t)wu_itoa((int64_t)len, m + k); m[k++] = ' ';
    for (const char *s = why; *s && k < sizeof m - 1; s++) m[k++] = *s;
    m[k] = 0; wp_log(1, m);
}
/* SOMA_PARCHE_028_UI: reduccion por promedio de bloques f x f (las fotos grandes ocupan 1/f^2 de memoria) */
static uint32_t *img_shrink(const uint32_t *src, int w, int h, int f, int *ow, int *oh)
{
    int nw = (w + f - 1) / f, nh = (h + f - 1) / f;
    uint32_t *dst = (uint32_t *)kmalloc((size_t)nw * nh * 4); if (!dst) return 0;
    for (int y = 0; y < nh; y++) {
        wu_tick();
        int y0 = y * f, y1 = y0 + f > h ? h : y0 + f;
        for (int x = 0; x < nw; x++) {
            int x0 = x * f, x1 = x0 + f > w ? w : x0 + f; uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) { const uint32_t *row = src + (size_t)yy * w; for (int xx = x0; xx < x1; xx++) { uint32_t c = row[xx]; a += c >> 24; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; } }
            dst[(size_t)y * nw + x] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    *ow = nw; *oh = nh; return dst;
}
#define WP_FAR (1 << 28)
/* Libera imagenes ya cargadas que estan lejos de la zona visible (vuelven a quedar pendientes: si el usuario se acerca se
 * piden otra vez, normalmente de la cache de la pasarela). Devuelve los bytes liberados. */
static uint32_t img_evict(struct wpage *p, uint32_t need, const struct wp_img *keep)
{
    uint32_t freed = 0; int lim = p->vh > 0 ? p->vh * 2 : 1200; if (lim < 1600) lim = 1600;   /* la pintura salta lo que esta a mas de 600 px */
    while (freed < need) {
        struct wp_img *far = 0;
        for (int i = 0; i < p->nimg; i++) { struct wp_img *x = &p->imgs[i]; if (x == keep || x->state != 2 || !x->px || x->dist <= lim) continue; if (!far || x->dist > far->dist) far = x; }
        if (!far) break;
        uint32_t by = (uint32_t)far->pw * (uint32_t)far->ph * 4;
        kfree(far->px); far->px = 0; far->state = 1; p->img_pending++; if (p->img_ok > 0) p->img_ok--;
        p->img_bytes = p->img_bytes > by ? p->img_bytes - by : 0; freed += by;
    }
    return freed;
}
static void img_decode_into(struct wpage *p, struct wp_img *e, const uint8_t *data, uint32_t len)
{
    struct wimg im; memset(&im, 0, sizeof im);
    if (len > 8 && wi_decode(data, len, &im) == 0 && im.px && im.w > 0 && im.h > 0) {
        /* SOMA_PARCHE_028_UI: se guarda al tamano al que se muestra (si se conoce), con un tope por imagen y dentro del
         * presupuesto de la pagina; si no cabe, se liberan antes las imagenes lejanas. La maquetacion sigue usando el tamano
         * natural (e->w, e->h), asi que la pagina se ve igual. */
        int f = 1;
        if (e->dw > 0 && e->dh > 0) { int fw = im.w / e->dw, fh = im.h / e->dh; f = fw < fh ? fw : fh; if (f < 1) f = 1; }
        for (;;) { uint64_t pix = (uint64_t)((im.w + f - 1) / f) * (uint64_t)((im.h + f - 1) / f); if (pix <= WP_IMG_STORE_PIX || f >= 16) break; f++; }
        uint32_t need = (uint32_t)((im.w + f - 1) / f) * (uint32_t)((im.h + f - 1) / f) * 4;
        uint32_t left = p->img_bytes < WP_IMG_BUDGET ? WP_IMG_BUDGET - p->img_bytes : 0;
        if (need > left) { img_evict(p, need - left, e); left = p->img_bytes < WP_IMG_BUDGET ? WP_IMG_BUDGET - p->img_bytes : 0; }
        while (need > left && f < 16) { f++; need = (uint32_t)((im.w + f - 1) / f) * (uint32_t)((im.h + f - 1) / f) * 4; }   /* ultimo recurso: mas pequena */
        int pw = im.w, ph = im.h; uint32_t *px = im.px;
        if (f > 1) { px = img_shrink(im.px, im.w, im.h, f, &pw, &ph); kfree(im.px); im.px = 0; }
        uint32_t bytes = (uint32_t)pw * (uint32_t)ph * 4;
        if (px && bytes <= left) {
            e->px = px; e->w = im.w; e->h = im.h; e->pw = pw; e->ph = ph; e->state = 2; p->img_bytes += bytes;
            int al = 0; for (uint32_t i = 0; i < (uint32_t)pw * (uint32_t)ph; i += 7) if ((px[i] >> 24) != 0xFF) { al = 1; break; }
            e->has_alpha = al; p->img_ok++;
            return;
        }
        if (px) kfree(px);
        e->state = 3; img_note_fail(p, e->url, "sin memoria para mas imagenes", 200, len);
        return;
    }
    wi_free(&im); e->state = 3; img_note_fail(p, e->url, len > 8 ? img_kind(data, len) : "vacia", 200, len);
}
/* SOMA_PARCHE_028_UI: formatos que el motor no decodifica pero la pasarela si convierte */
static int img_needs_gateway(const uint8_t *d, uint32_t n)
{
    if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return 1;
    if (n >= 12 && !memcmp(d + 4, "ftyp", 4)) return 1;
    if (n >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0) return 1;                 /* ICO */
    if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8) {                                             /* JPEG CMYK o aritmetico */
        for (uint32_t i = 2; i + 9 < n; ) {
            if (d[i] != 0xFF) { i++; continue; }
            int m = d[i + 1]; if (m == 0xFF) { i++; continue; }
            if (m == 0xDA || m == 0xD9) break;
            uint32_t l = ((uint32_t)d[i + 2] << 8) | d[i + 3];
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) return (m >= 0xC9) || d[i + 9] == 4;
            i += 2 + l;
        }
    }
    uint32_t i = 0; while (i < n && i < 64 && (d[i] == ' ' || d[i] == '\n' || d[i] == '\r' || d[i] == '\t' || d[i] >= 0x80)) i++;
    if (i + 5 < n && (!memcmp(d + i, "<svg", 4) || !memcmp(d + i, "<?xml", 5))) return 1;
    return 0;
}
/* Rasteriza en la pasarela un <svg> en linea (POST soma-gw:/svg) y lo deja como imagen de la pagina */
static void svg_load(struct wpage *p, int idx)
{
    struct wp_img *e = &p->imgs[idx]; struct wp_svg *sv = 0;
    uint32_t h = 0; for (const char *s = e->url + 9; wu_ishex(*s); s++) h = h * 16 + (uint32_t)wu_hexval(*s);
    for (int i = 0; i < p->nsvg; i++) if (p->svgs[i].hash == h) { sv = &p->svgs[i]; break; }
    if (!sv || !sv->markup || !(wn_gateway_caps() & WN_GW_SVG)) { e->state = 3; return; }
    static const char hx[] = "0123456789abcdef"; char url[48]; uint32_t k = wu_copy(url, "soma-gw:/svg?c=", sizeof url);
    for (int sh = 20; sh >= 0; sh -= 4) url[k++] = hx[(sv->color >> sh) & 15];
    url[k] = 0;
    struct wn_req rq; struct wn_resp rs; memset(&rq, 0, sizeof rq);
    rq.url = url; rq.method = "POST"; rq.body = sv->markup; rq.body_len = sv->len; rq.ctype = "image/svg+xml"; rq.timeout_ms = 8000; rq.max_body = 2u << 20; rq.want_binary = 1; rq.no_redirect = 1;
    if (wn_fetch(&rq, &rs) == 0 && rs.body && rs.status == 200 && rs.len > 8) img_decode_into(p, &p->imgs[idx], (const uint8_t *)rs.body, rs.len);
    else p->imgs[idx].state = 3;
    wn_free(&rs);
}
/* SOMA_PARCHE_028_UI: posicion de cada imagen respecto a la zona visible (con la ultima maquetacion). Se recalcula solo si
 * cambian la maquetacion, el desplazamiento o el numero de imagenes. */
static void img_scan(struct wpage *p)
{
    if (!p->lay || p->lay_busy || !p->doc) return;
    uint32_t key = p->gen * 2654435761u ^ (uint32_t)p->sy * 40503u ^ (uint32_t)p->vh * 97u ^ (uint32_t)p->nimg;
    if (key == p->img_scan_key && key) return;
    p->img_scan_key = key;
    for (int i = 0; i < p->nimg; i++) { p->imgs[i].dist = WP_FAR; p->imgs[i].dw = p->imgs[i].dh = 0; }
    char u[WN_URL_MAX];
    for (struct wnode *n = p->doc->root; n; ) {
        if (n->type == WN_ELEM && (n->tag == WT_IMG || n->tag == WT_SVG || n->tag == WT_INPUT) && n->box) {
            const char *src = wl_img_src(n);
            if (src && *src) {
                abs_url(p, src, u, sizeof u);
                struct wp_img *e = img_find(p, u, wu_hash(u, wu_strlen(u)));
                int x, y, w, h;
                if (e && wl_node_rect(p->lay, n, &x, &y, &w, &h) && n->st && n->st->display != D_NONE) {
                    int d = y > p->sy + p->vh ? y - (p->sy + p->vh) : (y + h < p->sy ? p->sy - (y + h) : 0);
                    if (d < e->dist) e->dist = d;
                    if (w > e->dw) e->dw = w;
                    if (h > e->dh) e->dh = h;
                }
            }
        }
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
}
/* carga la imagen pendiente mas cercana a la pantalla; devuelve 1 si hizo algo (0 = no queda ninguna cerca) */
static int img_load_one(struct wpage *p)
{
    img_scan(p);
    int best = -1, lim = p->vh > 0 ? p->vh + p->vh / 2 : 900, waiting = 0;
    for (int i = 0; i < p->nimg; i++) {
        struct wp_img *x = &p->imgs[i]; if (x->state != 1) continue;
        if (!p->lay || x->dist <= lim) { if (best < 0 || x->dist < p->imgs[best].dist) best = i; }
        else waiting++;
    }
    p->img_waiting = waiting;
    for (int i = best; i >= 0; i = -1) {
        struct wp_img *e = &p->imgs[i];
        p->img_pending--; e->tries++;
        struct wn_resp rs;
        if (wu_starts(e->url, "soma-svg:")) { svg_load(p, i); return 1; }      /* SOMA_PARCHE_028_UI */
        int lr = load_res(p, e->url, 0, 0, 0, 0, 1, 8u << 20, 10000, &rs);
        if (lr == 0 && rs.body && rs.status >= 200 && rs.status < 300 && !rs.via_gateway && wu_startsi(e->url, "http://") && img_needs_gateway((const uint8_t *)rs.body, rs.len)) {
            /* SOMA_PARCHE_028_UI: WebP/AVIF/SVG servido por http:// -> segundo intento a traves de la pasarela, que lo convierte */
            struct wn_resp r2; struct wn_req rq; memset(&rq, 0, sizeof rq);
            rq.url = e->url; rq.referer = p->url; rq.timeout_ms = 10000; rq.max_body = 8u << 20; rq.want_binary = 1; rq.via_gateway = 1; rq.accept = "image/png,image/jpeg,image/gif";
            if (wn_fetch(&rq, &r2) == 0 && r2.body && r2.status >= 200 && r2.status < 300 && !img_needs_gateway((const uint8_t *)r2.body, r2.len)) { wn_free(&rs); rs = r2; }
            else wn_free(&r2);
        }
        if (lr == 0 && rs.body && rs.status >= 200 && rs.status < 300) img_decode_into(p, &p->imgs[i], (const uint8_t *)rs.body, rs.len);
        else { p->imgs[i].state = 3; img_note_fail(p, e->url, rs.err[0] ? rs.err : (lr ? "error de red" : "estado HTTP"), rs.status, rs.len); }
        wn_free(&rs);
        return 1;
    }
    return 0;
}
static void img_seed(struct wpage *p, const char *url, const char *data, uint32_t len)
{
    uint32_t h = wu_hash(url, wu_strlen(url)); struct wp_img *e = img_find(p, url, h); if (!e) e = img_add(p, url, h);
    if (e) img_decode_into(p, e, (const uint8_t *)data, len);
}

/* ------------------------------------------------------------ hojas de estilo */
static int is_abs_ref(const char *s) { return has_scheme(s) || (s[0] == '/' && s[1] == '/') || s[0] == '#'; }
static void css_absolutize(const char *css, uint32_t n, const char *base, struct wb *o)
{
    for (uint32_t i = 0; i < n; ) {
        if (css[i] == '/' && i + 1 < n && css[i + 1] == '*') { uint32_t j = i + 2; while (j + 1 < n && !(css[j] == '*' && css[j + 1] == '/')) j++; j = j + 2 < n ? j + 2 : n; wb_addn(o, css + i, j - i); i = j; continue; }
        int imp = (i + 7 <= n && wu_strnieq(css + i, "@import", 7));
        int isurl = (i + 4 <= n && wu_strnieq(css + i, "url(", 4));
        if (imp) {
            uint32_t j = i + 7; wb_addn(o, css + i, 7);
            while (j < n && wu_isspace(css[j])) { wb_addc(o, css[j]); j++; }
            if (j < n && (css[j] == '"' || css[j] == '\'')) {
                char q = css[j]; uint32_t k = j + 1; while (k < n && css[k] != q) k++;
                char ref[WN_URL_MAX], res[WN_URL_MAX]; wu_copyn(ref, css + j + 1, k - j - 1 < sizeof ref - 1 ? k - j - 1 : sizeof ref - 1, sizeof ref);
                if (!is_abs_ref(ref) && wn_url_resolve(base, ref, res, sizeof res) == 0) { wb_addc(o, '"'); wb_add(o, res); wb_addc(o, '"'); } else wb_addn(o, css + j, k + 1 - j);
                i = k < n ? k + 1 : n; continue;
            }
            i = j; continue;
        }
        if (isurl) {
            uint32_t j = i + 4; while (j < n && wu_isspace(css[j])) j++;
            char q = 0; if (j < n && (css[j] == '"' || css[j] == '\'')) q = css[j++];
            uint32_t k = j; while (k < n && (q ? css[k] != q : css[k] != ')') && css[k] != '\n') k++;
            uint32_t e = k; while (e > j && wu_isspace(css[e - 1])) e--;
            char ref[WN_URL_MAX], res[WN_URL_MAX]; wu_copyn(ref, css + j, e - j < sizeof ref - 1 ? e - j : sizeof ref - 1, sizeof ref);
            if (q && k < n) k++;
            while (k < n && css[k] != ')') k++;
            if (!is_abs_ref(ref) && ref[0] && wn_url_resolve(base, ref, res, sizeof res) == 0) { wb_add(o, "url(\""); wb_add(o, res); wb_add(o, "\")"); }
            else { wb_add(o, "url(\""); wb_add(o, ref); wb_add(o, "\")"); }
            i = k < n ? k + 1 : n; continue;
        }
        wb_addc(o, css[i]); i++;
    }
}
static void add_sheet_text(struct wpage *p, const char *css, uint32_t n, const char *media, const char *base_for_urls)
{
    struct wb t = {0};
    const char *src = css; uint32_t sl = n;
    if (base_for_urls) { css_absolutize(css, n, base_for_urls, &t); src = t.p ? t.p : ""; sl = t.len; }
    int wrap = media && *media && !wu_strieq(media, "all") && !wu_strieq(media, "screen");
    if (wrap) {
        struct wb w = {0}; wb_add(&w, "@media "); wb_add(&w, media); wb_add(&w, "{"); wb_addn(&w, src, sl); wb_add(&w, "}");
        if (w.p) wc_add_sheet(p->css, w.p, w.len, 1);
        wb_free(&w);
    } else if (sl) wc_add_sheet(p->css, src, sl, 1);
    if (!p->hover_rules && wu_find(src, ":hover")) p->hover_rules = 1;
    wb_free(&t);
}
static int rel_has(const char *rel, const char *tok)
{
    uint32_t tl = wu_strlen(tok); if (!rel) return 0;
    for (const char *s = rel; *s; ) {
        while (wu_isspace(*s)) s++;
        const char *e = s; while (*e && !wu_isspace(*e)) e++;
        if ((uint32_t)(e - s) == tl && wu_strnieq(s, tok, tl)) return 1;
        s = e;
    }
    return 0;
}
static void fetch_sheet(struct wpage *p, const char *url, const char *media)
{
    struct wn_resp rs; set_status(p, WPS_BUSY, "Descargando hoja de estilo...");
    if (load_res(p, url, 0, 0, 0, 0, 0, 1u << 20, 12000, &rs) == 0 && rs.body && rs.status >= 200 && rs.status < 300) {
        to_utf8(&rs, 0);
        add_sheet_text(p, rs.body, rs.len, media, rs.final_url[0] ? rs.final_url : url);
    } else { char m[160] = "CSS no disponible: "; wu_copy(m + 19, url, sizeof m - 19); wp_log(1, m); }
    wn_free(&rs);
}
static void scan_styles(struct wpage *p, int allow_net)
{
    if (!p->doc || !p->css) return;
    struct wnode *n = p->doc->root; int nlinks = 0;
    while (n) {
        if (n->type == WN_ELEM && n->sel != 7 && (n->flags & WNF_INDOC || n->parent)) {
            if (n->tag == WT_STYLE) {
                n->sel = 7; const char *ty = wd_attr(n, "type");
                if (!ty || !*ty || wu_findi(ty, "css")) { struct wb t = {0}; wd_text_content(n, &t); if (t.len) add_sheet_text(p, t.p, t.len, wd_attr(n, "media"), 0); wb_free(&t); }
            } else if (n->tag == WT_LINK && rel_has(wd_attr(n, "rel"), "stylesheet") && !rel_has(wd_attr(n, "rel"), "alternate")) {
                const char *href = wd_attr(n, "href");
                if (href && *href) {
                    if (!allow_net && !wu_startsi(href, "data:")) { p->styles_pending = 1; }
                    else { n->sel = 7; if (nlinks++ < 30 && !p->stop) { char u[WN_URL_MAX]; abs_url(p, href, u, sizeof u); fetch_sheet(p, u, wd_attr(n, "media")); } }
                } else n->sel = 7;
            }
        }
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    const char *imp; int guard = 0;
    while (allow_net && guard++ < 24 && (imp = wc_next_import(p->css)) != 0 && !p->stop) { char u[WN_URL_MAX]; wu_copy(u, imp, sizeof u); fetch_sheet(p, u, 0); }
}

/* ------------------------------------------------------------ preparacion del DOM */
static const char *srcset_pick(const char *ss, char *out, uint32_t max)
{
    /* elige el candidato de mayor densidad/anchura razonable (<= 1200w o 2x) */
    int best_score = -1; out[0] = 0;
    for (const char *s = ss; *s; ) {
        while (wu_isspace(*s) || *s == ',') s++;
        const char *u = s; while (*s && !wu_isspace(*s) && *s != ',') s++;
        uint32_t ul = (uint32_t)(s - u); if (!ul) break;
        while (wu_isspace(*s)) s++;
        int score = 100;
        if (*s && *s != ',') { int v = wu_atoi(s); const char *e = s; while (wu_isdigit(*e) || *e == '.') e++; score = *e == 'w' ? v : *e == 'x' ? v * 600 : 100; if (*e == 'w' && v > 1400) score = 1400 - (v - 1400) / 4; while (*s && *s != ',') s++; }
        if (score > best_score) { best_score = score; wu_copyn(out, u, ul, max); }
    }
    return out[0] ? out : 0;
}
static const char *const k_lazy_attrs[] = { "data-src", "data-lazy-src", "data-original", "data-lazy", "data-url", "data-img", "data-image", "data-hi-res-src", "data-full-src", 0 };
static const char *const k_lazy_sets[] = { "data-srcset", "data-lazy-srcset", "data-original-set", 0 };
static int src_is_placeholder(const char *s)
{
    if (!s || !*s) return 1;
    if (wu_starts(s, "data:")) return 1;
    if (wu_findi(s, "placeholder") || wu_findi(s, "blank.") || wu_findi(s, "spacer") || wu_findi(s, "transparent.") || wu_findi(s, "lazy.") || wu_findi(s, "1x1")) return 1;
    return 0;
}
static int ext_unsupported(const char *u)
{
    uint32_t n = 0; while (u[n] && u[n] != '?' && u[n] != '#') n++;
    static const char *const bad[] = { ".webp", ".avif", ".svg", ".heic", 0 };
    int caps = wn_gateway_caps();                         /* SOMA_PARCHE_028_UI: con la pasarela nueva, WebP/AVIF/SVG si se pueden pedir */
    for (int i = 0; bad[i]; i++) {
        if ((i < 2 && (caps & WN_GW_IMAGES)) || (i == 2 && (caps & WN_GW_SVG))) continue;
        uint32_t l = wu_strlen(bad[i]); if (n > l) { uint32_t k = 0; while (k < l && ((u[n - l + k] | 0x20) == bad[i][k])) k++; if (k == l) return 1; } }
    return 0;
}
/* Imagenes diferidas (lazy-load): sin JavaScript de scroll, el atributo real esta en data-*; lo promovemos a src. */
static void lazy_promote(struct wpage *p, struct wnode *n)
{
    const char *src = wd_attr(n, "src"); char pick[WN_URL_MAX];
    int bad = src_is_placeholder(src) || ext_unsupported(src);
    if (!bad) return;
    for (int i = 0; k_lazy_attrs[i]; i++) {
        const char *v = wd_attr(n, k_lazy_attrs[i]);
        if (v && *v && !wu_starts(v, "data:") && !ext_unsupported(v)) { wd_set_attr(p->doc, n, "src", wa_strdup(&p->doc->ar, v)); return; }
    }
    for (int i = 0; k_lazy_sets[i]; i++) {
        const char *v = wd_attr(n, k_lazy_sets[i]);
        if (v && *v && srcset_pick(v, pick, sizeof pick) && !ext_unsupported(pick)) { wd_set_attr(p->doc, n, "src", wa_strdup(&p->doc->ar, pick)); return; }
    }
}
/* SOMA_PARCHE_028_UI: <svg> en linea (iconos y logotipos). El motor no dibuja vectores: se guarda el marcado de cada
 * <svg> visible y la pasarela lo rasteriza; el elemento se maqueta como una imagen con la clave "soma-svg:<hash>". */
static void prep_svg(struct wpage *p)
{
    if (!(wn_gateway_caps() & WN_GW_SVG)) return;
    struct wnode *n = p->doc->root;
    while (n) {
        int skip = 0;
        if (n->type == WN_ELEM && n->tag == WT_SVG) {
            skip = 1;
            if (n->st && n->st->display != D_NONE) {
                struct wb b = {0}; uint32_t col = n->st->color & 0xFFFFFF;
                wb_add(&b, "<svg");
                for (struct wattr *a = n->attrs; a; a = a->next) {
                    if (wu_streq(a->name, "data-soma-svg") || wu_streq(a->name, "class") || wu_streq(a->name, "id") || wu_starts(a->name, "aria-") || wu_starts(a->name, "data-")) continue;
                    wb_addc(&b, ' '); wb_add(&b, a->name); wb_add(&b, "=\"");
                    for (const char *s = a->val ? a->val : ""; *s; s++) { if (*s == '"') wb_add(&b, "&quot;"); else if (*s == '&') wb_add(&b, "&amp;"); else if (*s == '<') wb_add(&b, "&lt;"); else wb_addc(&b, *s); }
                    wb_addc(&b, '"');
                }
                wb_addc(&b, '>'); wd_outer_html(n, &b, 1); wb_add(&b, "</svg>");
                if (!b.err && b.p && b.len > 24 && b.len <= WP_SVG_MAX_BYTES) {
                    uint32_t h = wu_hash(b.p, b.len) ^ (col * 2654435761u); if (!h) h = 1;
                    static const char hx[] = "0123456789abcdef"; char key[24]; uint32_t k = wu_copy(key, "soma-svg:", sizeof key);
                    for (int sh = 28; sh >= 0; sh -= 4) key[k++] = hx[(h >> sh) & 15];
                    key[k] = 0;
                    const char *cur = wd_attr(n, "data-soma-svg");
                    if (!cur || !wu_streq(cur, key)) {
                        int found = 0; for (int i = 0; i < p->nsvg; i++) if (p->svgs[i].hash == h) { found = 1; break; }
                        if (!found && p->nsvg < WP_MAX_SVG) {
                            if (!p->svgs) { p->svgs = (struct wp_svg *)kmalloc(WP_MAX_SVG * sizeof(struct wp_svg)); if (p->svgs) memset(p->svgs, 0, WP_MAX_SVG * sizeof(struct wp_svg)); }
                            if (p->svgs) { struct wp_svg *sv = &p->svgs[p->nsvg++]; sv->hash = h; sv->markup = b.p; sv->len = b.len; sv->color = col; b.p = 0; found = 1; }
                        }
                        if (found) wd_set_attr(p->doc, n, "data-soma-svg", wa_strdup(&p->doc->ar, key));
                    }
                }
                wb_free(&b);
            }
        }
        if (n->first && !skip) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
}
static void set_src_once(struct wpage *p, struct wnode *n, const char *v)
{
    const char *cur = wd_attr(n, "src"); if (cur && wu_streq(cur, v)) return;
    wd_set_attr(p->doc, n, "src", wa_strdup(&p->doc->ar, v));
}
static void prep_dom(struct wpage *p, int imgs_only)
{
    struct wnode *n = p->doc->root;
    while (n) {
        if (n->type == WN_ELEM) {
            if (n->tag == WT_INPUT) {
                const char *t = wd_attr(n, "type");
                if (!imgs_only && t && (wu_strieq(t, "checkbox") || wu_strieq(t, "radio")) && wd_attr(n, "checked")) n->flags |= WNF_CHECKED;
            } else if (n->tag == WT_IMG) {
                const char *src = wd_attr(n, "src"); char pick[WN_URL_MAX];
                if (n->parent && n->parent->tag == WT_PICTURE) {
                    for (struct wnode *s = n->parent->first; s && s != n; s = s->next) {
                        if (s->type != WN_ELEM || s->tag != WT_SOURCE) continue;
                        const char *ty = wd_attr(s, "type"); if (ty && (wu_findi(ty, "webp") || wu_findi(ty, "avif") || wu_findi(ty, "svg"))) continue;
                        const char *ss = wd_attr(s, "srcset"); if (!ss || !*ss) ss = wd_attr(s, "data-srcset");
                        if (ss && srcset_pick(ss, pick, sizeof pick) && !ext_unsupported(pick)) { set_src_once(p, n, pick); src = wd_attr(n, "src"); break; }
                    }
                }
                const char *ss = wd_attr(n, "srcset");
                if (ss && (!src || !*src || wu_starts(src, "data:")) && srcset_pick(ss, pick, sizeof pick) && !ext_unsupported(pick)) wd_set_attr(p->doc, n, "src", wa_strdup(&p->doc->ar, pick));
                lazy_promote(p, n);
            }
        }
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
}

/* ------------------------------------------------------------ maquetacion */
static void update_title(struct wpage *p)
{
    const char *t = p->doc && p->doc->title ? p->doc->title : ""; uint32_t k = 0;
    for (const char *s = t; *s && k + 1 < sizeof p->title; s++) { if (*s == '\n' || *s == '\r' || *s == '\t') { if (k && p->title[k - 1] != ' ') p->title[k++] = ' '; } else p->title[k++] = *s; }
    p->title[k] = 0;
    if (p->hpos >= 0 && p->hpos < p->hn) wu_copy(p->hist[p->hpos].title, p->title, sizeof p->hist[0].title);
}
static void relayout(struct wpage *p, int allow_net)
{
    if (!p->doc || !p->css || p->vw < 16) return;
    scan_styles(p, allow_net);
    uint64_t t0 = now_ms();
    p->lay_busy = 1;
    prep_dom(p, 1);
    wc_compute(p->css, p->vw, p->vh);
    prep_svg(p);                 /* necesita el estilo calculado (color actual) */
    struct wlay *nl = wl_new(p->doc, p->css, img_cb, p);
    if (!nl) { p->lay_busy = 0; return; }
    nl->ar.limit = 14u << 20; nl->focus = p->focus; nl->hover = p->hover; nl->caret_on = p->caret_on;
    wl_layout(nl, p->vw, p->vh);
    struct wlay *old = p->lay; p->lay = nl; if (old) wl_free(old);
    p->lay_busy = 0; p->gen++; p->layout_dur = (uint32_t)(now_ms() - t0);
    p->need_style = 0; p->last_layout_ms = now_ms();
    update_title(p);
    int mx = nl->doc_h - p->vh; if (mx < 0) mx = 0; if (p->sy > mx) p->sy = mx;
    mx = nl->doc_w - p->vw; if (mx < 0) mx = 0; if (p->sx > mx) p->sx = mx;
}
static void ensure_layout(struct wpage *p)
{
    if (p->need_style && p->forced < 40) { p->forced++; relayout(p, 0); }
}

/* ------------------------------------------------------------ formularios */
static int ctl_disabled(const struct wnode *n) { return (n->flags & WNF_DISABLED) || wd_attr(n, "disabled"); }
static void add_pair(struct wb *q, const char *name, const char *val, int multipart, const char *boundary)
{
    if (multipart) {
        wb_add(q, "--"); wb_add(q, boundary); wb_add(q, "\r\nContent-Disposition: form-data; name=\""); wb_add(q, name); wb_add(q, "\"\r\n\r\n"); wb_add(q, val); wb_add(q, "\r\n");
    } else {
        if (q->len) wb_addc(q, '&');
        pct_enc(q, name, 1); wb_addc(q, '='); pct_enc(q, val, 1);
    }
}
static void crlf_text(struct wb *o, const char *s) { for (; *s; s++) { if (*s == '\r') continue; if (*s == '\n') wb_add(o, "\r\n"); else wb_addc(o, *s); } }
static void form_data(struct wnode *form, struct wnode *submitter, struct wb *q, int multipart, const char *boundary)
{
    struct wnode *n = form->first;
    while (n) {
        if (n->type == WN_ELEM && !ctl_disabled(n)) {
            const char *name = wd_attr(n, "name");
            if (name && *name) {
                if (n->tag == WT_INPUT) {
                    const char *t = wd_attr(n, "type"); char tl[16]; wu_copy(tl, t ? t : "text", sizeof tl); for (char *c = tl; *c; c++) *c = (char)wu_lower(*c);
                    if (wu_streq(tl, "submit") || wu_streq(tl, "image") || wu_streq(tl, "button")) { if (n == submitter) { const char *v = wl_ctl_text(n); add_pair(q, name, v, multipart, boundary); } }
                    else if (wu_streq(tl, "reset") || wu_streq(tl, "file")) { if (wu_streq(tl, "file") && multipart) add_pair(q, name, "", multipart, boundary); }
                    else if (wu_streq(tl, "checkbox") || wu_streq(tl, "radio")) { if (n->flags & WNF_CHECKED) { const char *v = wd_attr(n, "value"); add_pair(q, name, v ? v : "on", multipart, boundary); } }
                    else add_pair(q, name, wl_ctl_text(n), multipart, boundary);
                } else if (n->tag == WT_TEXTAREA) {
                    struct wb t = {0}; crlf_text(&t, wl_ctl_text(n)); add_pair(q, name, t.p ? t.p : "", multipart, boundary); wb_free(&t);
                } else if (n->tag == WT_SELECT) {
                    int cnt = wl_select_count(n); int si = wl_select_index(n);
                    if (wd_attr(n, "multiple")) { for (int i = 0; i < cnt; i++) { struct wnode *o = wl_select_option(n, i); if (o && wd_attr(o, "selected")) { const char *v = wd_attr(o, "value"); struct wb tx = {0}; if (!v) { wd_text_content(o, &tx); v = tx.p; } add_pair(q, name, v ? v : "", multipart, boundary); wb_free(&tx); } } }
                    else if (cnt > 0) { struct wnode *o = wl_select_option(n, si); if (o) { const char *v = wd_attr(o, "value"); struct wb tx = {0}; if (!v) { wd_text_content(o, &tx); v = tx.p; } add_pair(q, name, v ? v : "", multipart, boundary); wb_free(&tx); } }
                } else if (n->tag == WT_BUTTON) { if (n == submitter) { const char *v = wd_attr(n, "value"); add_pair(q, name, v ? v : "", multipart, boundary); } }
            }
        }
        if (n->first) { n = n->first; continue; }
        while (n && n != form && !n->next) n = n->parent;
        if (!n || n == form) break;
        n = n->next;
    }
    if (multipart) { wb_add(q, "--"); wb_add(q, boundary); wb_add(q, "--\r\n"); }
}
static void queue_nav(struct wpage *p, const char *url, int mode)
{
    wu_copy(p->nav_url, url, sizeof p->nav_url); p->nav_mode = mode; p->nav_pending = 1; p->nav_at = 0;
}
static void form_submit(struct wpage *p, struct wnode *form, struct wnode *submitter)
{
    if (!form) return;
    const char *meth = submitter ? wd_attr(submitter, "formmethod") : 0; if (!meth) meth = wd_attr(form, "method");
    const char *act = submitter ? wd_attr(submitter, "formaction") : 0; if (!act) act = wd_attr(form, "action");
    const char *enc = submitter ? wd_attr(submitter, "formenctype") : 0; if (!enc) enc = wd_attr(form, "enctype");
    char url[WN_URL_MAX];
    if (act && *act) abs_url(p, act, url, sizeof url); else wu_copy(url, p->url, sizeof url);
    int post = meth && wu_strieq(meth, "post"); int multipart = post && enc && wu_findi(enc, "multipart");
    const char *boundary = "----SomaFormBoundary7MA4YWxkTrZu0gW";
    struct wb q = {0}; form_data(form, submitter, &q, multipart, boundary);
    if (post) {
        if (p->nav_post) kfree(p->nav_post);
        p->nav_post = (char *)kmalloc(q.len + 1); if (!p->nav_post) { wb_free(&q); return; }
        memcpy(p->nav_post, q.p ? q.p : "", q.len); p->nav_post[q.len] = 0; p->nav_plen = q.len;
        if (multipart) { wu_copy(p->nav_ctype, "multipart/form-data; boundary=", sizeof p->nav_ctype); wu_copy(p->nav_ctype + 30, boundary, sizeof p->nav_ctype - 30); }
        else wu_copy(p->nav_ctype, "application/x-www-form-urlencoded", sizeof p->nav_ctype);
        queue_nav(p, url, 6);
    } else {
        char *hash = (char *)wu_strchr(url, '#'); char frag[128]; frag[0] = 0; if (hash) { wu_copy(frag, hash, sizeof frag); *hash = 0; }
        char *qm = (char *)wu_strchr(url, '?'); if (qm) *qm = 0;
        struct wb u = {0}; wb_add(&u, url); wb_addc(&u, '?'); wb_addn(&u, q.p ? q.p : "", q.len); wb_add(&u, frag);
        queue_nav(p, u.p ? u.p : url, 0); wb_free(&u);
    }
    wb_free(&q);
}
static void form_reset(struct wpage *p, struct wnode *form)
{
    struct wnode *n = form->first;
    while (n) {
        if (n->type == WN_ELEM) {
            if (n->tag == WT_INPUT || n->tag == WT_TEXTAREA || n->tag == WT_SELECT) { n->value = 0; n->flags &= (uint16_t)~(WNF_EDITED | WNF_CHECKED); n->sel = 0; if (n->tag == WT_INPUT && wd_attr(n, "checked")) n->flags |= WNF_CHECKED; }
        }
        if (n->first) { n = n->first; continue; }
        while (n && n != form && !n->next) n = n->parent;
        if (!n || n == form) break;
        n = n->next;
    }
    (void)p;
}
static struct wnode *enclosing_form(struct wnode *n) { for (; n; n = n->parent) if (n->type == WN_ELEM && n->tag == WT_FORM) return n; return 0; }

/* ------------------------------------------------------------ estilo calculado para getComputedStyle */
static void fmt_color(uint32_t c, char *out, uint32_t max)
{
    struct wb b = {0}; uint32_t a = c >> 24;
    if (a == 0) { wu_copy(out, "rgba(0, 0, 0, 0)", max); return; }
    wb_add(&b, a == 255 ? "rgb(" : "rgba("); wb_addint(&b, (c >> 16) & 255); wb_add(&b, ", "); wb_addint(&b, (c >> 8) & 255); wb_add(&b, ", "); wb_addint(&b, c & 255);
    if (a != 255) { wb_add(&b, ", "); wb_addint(&b, a * 100 / 255 / 100); wb_addc(&b, '.'); wb_addint(&b, a * 100 / 255 % 100); }
    wb_addc(&b, ')'); wu_copy(out, b.p ? b.p : "", max); wb_free(&b);
}
static void fmt_px(int v, char *out, uint32_t max) { char t[24]; int n = wu_itoa(v, t); t[n++] = 'p'; t[n++] = 'x'; t[n] = 0; wu_copy(out, t, max); }
static int host_computed(void *ud, struct wnode *n, const char *prop, char *out, uint32_t max)
{
    struct wpage *p = (struct wpage *)ud; ensure_layout(p);
    const struct wstyle *s = n ? n->st : 0; if (!s) return -1;
    static const char *DISP[] = { "none", "block", "inline", "inline-block", "list-item", "flex", "inline-flex", "table", "table-row", "table-cell", "table-row-group", "table-caption", "grid", "contents", "inline-table" };
    static const char *POS[] = { "static", "relative", "absolute", "fixed", "sticky" };
    static const char *OVF[] = { "visible", "hidden", "scroll", "auto" };
    static const char *TA[] = { "left", "center", "right", "justify" };
    int bw = n->box ? n->box->w : 0, bh = n->box ? n->box->h : 0;
    if (wu_streq(prop, "display")) wu_copy(out, s->display < 15 ? DISP[s->display] : "block", max);
    else if (wu_streq(prop, "visibility")) wu_copy(out, s->visibility ? "hidden" : "visible", max);
    else if (wu_streq(prop, "position")) wu_copy(out, s->position < 5 ? POS[s->position] : "static", max);
    else if (wu_streq(prop, "float")) wu_copy(out, s->floating == FL_LEFT ? "left" : s->floating == FL_RIGHT ? "right" : "none", max);
    else if (wu_streq(prop, "color")) fmt_color(s->color, out, max);
    else if (wu_streq(prop, "background-color")) fmt_color(s->bg_color, out, max);
    else if (wu_streq(prop, "width")) { if (n->box) fmt_px(bw, out, max); else wu_copy(out, "auto", max); }
    else if (wu_streq(prop, "height")) { if (n->box) fmt_px(bh, out, max); else wu_copy(out, "auto", max); }
    else if (wu_streq(prop, "font-size")) fmt_px(s->font_px, out, max);
    else if (wu_streq(prop, "font-weight")) wu_copy(out, s->bold ? "700" : "400", max);
    else if (wu_streq(prop, "font-style")) wu_copy(out, s->italic ? "italic" : "normal", max);
    else if (wu_streq(prop, "font-family")) wu_copy(out, s->mono ? "monospace" : "sans-serif", max);
    else if (wu_streq(prop, "opacity")) { char t[16]; int v = s->opacity ? s->opacity : 255; int k = wu_itoa(v * 100 / 255, t); t[k] = 0; if (v == 255) wu_copy(out, "1", max); else { wu_copy(out, "0.", max); wu_copy(out + 2, t, max - 2); } }
    else if (wu_streq(prop, "overflow") || wu_streq(prop, "overflow-x")) wu_copy(out, s->overflow_x < 4 ? OVF[s->overflow_x] : "visible", max);
    else if (wu_streq(prop, "overflow-y")) wu_copy(out, s->overflow_y < 4 ? OVF[s->overflow_y] : "visible", max);
    else if (wu_streq(prop, "text-align")) wu_copy(out, s->text_align < 4 ? TA[s->text_align] : "left", max);
    else if (wu_streq(prop, "z-index")) { if (s->z_index) { char t[16]; int k = wu_itoa(s->z_index, t); t[k] = 0; wu_copy(out, t, max); } else wu_copy(out, "auto", max); }
    else if (wu_streq(prop, "cursor")) wu_copy(out, s->pointer ? "pointer" : "auto", max);
    else if (wu_streq(prop, "box-sizing")) wu_copy(out, s->box_sizing ? "border-box" : "content-box", max);
    else if (wu_streq(prop, "text-decoration") || wu_streq(prop, "text-decoration-line")) wu_copy(out, s->text_decor & 1 ? "underline" : s->text_decor & 2 ? "line-through" : s->text_decor & 4 ? "overline" : "none", max);
    else if (wu_streq(prop, "line-height")) { if (s->lh_mode == 0) wu_copy(out, "normal", max); else fmt_px(wc_line_px(s), out, max); }
    else if (wu_startsi(prop, "margin-")) { int i = wu_streq(prop + 7, "top") ? 0 : wu_streq(prop + 7, "right") ? 1 : wu_streq(prop + 7, "bottom") ? 2 : 3; fmt_px(s->margin[i].kind == LK_VAL ? s->margin[i].px : 0, out, max); }
    else if (wu_startsi(prop, "padding-")) { int i = wu_streq(prop + 8, "top") ? 0 : wu_streq(prop + 8, "right") ? 1 : wu_streq(prop + 8, "bottom") ? 2 : 3; fmt_px(s->padding[i].kind == LK_VAL ? s->padding[i].px : 0, out, max); }
    else if (wu_startsi(prop, "border-") && wu_find(prop, "-width")) { int i = wu_startsi(prop + 7, "top") ? 0 : wu_startsi(prop + 7, "right") ? 1 : wu_startsi(prop + 7, "bottom") ? 2 : 3; fmt_px(s->border_w[i], out, max); }
    else return -1;
    return 0;
}

/* ------------------------------------------------------------ servicios para el motor JS */
static void host_log(void *ud, int level, const char *msg) { (void)ud; wp_log(level, msg); }
static void host_dirty(void *ud, int what) { struct wpage *p = (struct wpage *)ud; if (what == 1) p->need_style = 1; }
static void same_doc_nav(struct wpage *p, const char *url, int replace);
static void host_navigate(void *ud, const char *url, int mode)
{
    struct wpage *p = (struct wpage *)ud;
    if (mode == 2 || mode == 3) {                       /* pushState / replaceState */
        wu_copy(p->url, url, sizeof p->url); if (p->doc) wu_copy(p->doc->base, url, sizeof p->doc->base);
        if (mode == 3 && p->hpos >= 0 && p->hpos < p->hn) wu_copy(p->hist[p->hpos].url, url, sizeof p->hist[0].url);
        else if (mode == 2) {
            if (p->hpos >= 0) p->hn = p->hpos + 1;
            if (p->hn >= WP_HIST) { memmove(&p->hist[0], &p->hist[1], sizeof p->hist[0] * (WP_HIST - 1)); p->hn = WP_HIST - 1; }
            wu_copy(p->hist[p->hn].url, url, sizeof p->hist[0].url); wu_copy(p->hist[p->hn].title, p->title, sizeof p->hist[0].title); p->hist[p->hn].scroll_y = 0; p->hpos = p->hn; p->hn++;
        }
        return;
    }
    if (mode == 0 || mode == 1) {
        const char *a = p->url, *b = url; uint32_t la = 0, lb = 0; while (a[la] && a[la] != '#') la++; while (b[lb] && b[lb] != '#') lb++;
        if (la == lb && wu_strneq(a, b, la) && wu_strchr(url, '#') && p->doc) { same_doc_nav(p, url, mode == 1); return; }
    }
    queue_nav(p, url, mode == 4 ? 4 : mode);
}
static int host_fetch(void *ud, const char *method, const char *url, const char *body, uint32_t blen, const char *rct, int *status, char **out, uint32_t *outlen, char *ctype, uint32_t ctmax)
{
    struct wpage *p = (struct wpage *)ud; struct wn_resp rs;
    int rc = load_res(p, url, method, body, blen, rct, 1, 4u << 20, 15000, &rs);
    if (rc != 0) { wn_free(&rs); return -1; }
    *status = rs.status; *out = rs.body; *outlen = rs.len; rs.body = 0; wu_copy(ctype, rs.ctype, ctmax);
    return 0;
}
static void host_submit(void *ud, struct wnode *form) { form_submit((struct wpage *)ud, form, 0); }
static void activate_node(struct wpage *p, struct wnode *n, int x, int y);
static void host_activate(void *ud, struct wnode *n) { activate_node((struct wpage *)ud, n, 0, 0); }
static void blur_focus(struct wpage *p);
static void host_focus(void *ud, struct wnode *n)
{
    struct wpage *p = (struct wpage *)ud; p->focus = n; p->focus_orig = n ? (n->value ? n->value : wd_attr(n, "value")) : 0;
    if (p->lay) p->lay->focus = n;
}
static void host_load_script(void *ud, struct wnode *s)
{
    struct wpage *p = (struct wpage *)ud; const char *src = wd_attr(s, "src"); if (!src || !p->js) return;
    char u[WN_URL_MAX]; abs_url(p, src, u, sizeof u); struct wn_resp rs; int ok = 0;
    if (load_res(p, u, 0, 0, 0, 0, 0, WP_MAX_RES, 12000, &rs) == 0 && rs.body && rs.status >= 200 && rs.status < 300) {
        to_utf8(&rs, 0); wjs_set_script(p->js, s); wjs_eval(p->js, rs.body, rs.len, u); wjs_set_script(p->js, 0); ok = 1;
    }
    wn_free(&rs);
    wjs_dispatch(p->js, s, ok ? "load" : "error", 0, 0, 0, 0, 0);
}
static void host_history(void *ud, int delta) { struct wpage *p = (struct wpage *)ud; p->nav_delta = delta; p->nav_mode = 5; p->nav_pending = 1; }
static void host_alert(void *ud, const char *msg) { struct wpage *p = (struct wpage *)ud; wu_copy(p->alert_msg, msg, sizeof p->alert_msg); p->alert_pending = 1; }
static const char *host_get_cookie(void *ud, char *buf, uint32_t max) { struct wpage *p = (struct wpage *)ud; wn_cookie_header(p->url, buf, max, 1); return buf; }
static void host_set_cookie(void *ud, const char *c) { struct wpage *p = (struct wpage *)ud; wn_cookie_store(p->url, c); }
static const char *host_storage(void *ud, int local, const char *key, const char *val, int op)
{
    struct wpage *p = (struct wpage *)ud; char origin[160]; wn_url_origin(p->url, origin, sizeof origin);
    if (!local && !p->ss) { p->ss = (struct wp_store *)kmalloc(sizeof *p->ss); if (!p->ss) return 0; memset(p->ss, 0, sizeof *p->ss); }
    return store_op(local ? &g_local : p->ss, origin, key ? key : "", val, op);
}
static void host_layout_rect(void *ud, struct wnode *n, int *x, int *y, int *w, int *h)
{
    struct wpage *p = (struct wpage *)ud; *x = *y = *w = *h = 0; ensure_layout(p);
    if (p->lay) wl_node_rect(p->lay, n, x, y, w, h);
}
static void host_viewport(void *ud, int *w, int *h, int *sx, int *sy) { struct wpage *p = (struct wpage *)ud; *w = p->vw; *h = p->vh; *sx = p->sx; *sy = p->sy; }
static void host_scroll_to(void *ud, int x, int y) { struct wpage *p = (struct wpage *)ud; p->sx = x < 0 ? 0 : x; p->sy = y < 0 ? 0 : y; }

static void host_init(struct wpage *p)
{
    struct wjs_host *h = &p->host; memset(h, 0, sizeof *h);
    h->ud = p; h->log = host_log; h->dirty = host_dirty; h->navigate = host_navigate; h->fetch = host_fetch; h->submit = host_submit; h->activate = host_activate;
    h->focus = host_focus; h->load_script = host_load_script; h->history = host_history; h->alert = host_alert; h->get_cookie = host_get_cookie; h->set_cookie = host_set_cookie;
    h->storage = host_storage; h->layout_rect = host_layout_rect; h->viewport = host_viewport; h->scroll_to = host_scroll_to; h->computed = host_computed;
    h->url = p->url; h->ua = WP_UA;
}

/* ------------------------------------------------------------ documento */
static void reset_doc(struct wpage *p)
{
    struct wlay *l = p->lay; p->lay = 0;
    if (p->js) { wjs_free(p->js); p->js = 0; }
    if (l) wl_free(l);
    if (p->css) { wc_free(p->css); p->css = 0; }
    if (p->doc) { wd_free(p->doc); p->doc = 0; }
    img_free_all(p); p->img_ok = p->img_fail = p->img_logged = 0; p->img_why[0] = 0;
    p->focus = p->hover = 0; p->dd_sel = 0; p->focus_orig = 0; p->hover_rules = 0; p->styles_pending = 0; p->need_style = 0; p->caret_on = 0; p->nav_at = 0; p->forced = 0;
}
static void check_meta_refresh(struct wpage *p)
{
    for (struct wnode *n = p->doc->head ? p->doc->head->first : 0; n; n = n->next) {
        if (n->type != WN_ELEM || n->tag != WT_META) continue;
        const char *he = wd_attr(n, "http-equiv"); if (!he || !wu_strieq(he, "refresh")) continue;
        const char *c = wd_attr(n, "content"); if (!c) continue;
        int secs = wu_atoi(c); const char *u = wu_findi(c, "url="); if (!u) continue; u += 4; if (*u == '\'' || *u == '"') u++;
        char ref[WN_URL_MAX]; uint32_t k = 0; while (*u && *u != '\'' && *u != '"' && k + 1 < sizeof ref) ref[k++] = *u++; ref[k] = 0;
        if (!ref[0]) continue;
        abs_url(p, ref, p->nav_url, sizeof p->nav_url); p->nav_mode = 1; p->nav_pending = 0; p->nav_at = now_ms() + (uint64_t)(secs < 0 ? 0 : secs > 10 ? 10 : secs) * 1000 + 50;
        return;
    }
}
static int build_document(struct wpage *p, const char *html, uint32_t len, const char *url)
{
    reset_doc(p);
    p->doc = wd_new(); if (!p->doc) return -1;
    p->doc->ar.limit = 16u << 20;
    wu_copy(p->doc->base, url, sizeof p->doc->base);
    wd_parse(p->doc, html, len);
    wu_copy(p->doc->base, url, sizeof p->doc->base);
    for (struct wnode *n = p->doc->head ? p->doc->head->first : 0; n; n = n->next) {          /* <base href> */
        if (n->type == WN_ELEM && n->tag == WT_BASE && wd_attr(n, "href")) { char b[WN_URL_MAX]; if (wn_url_resolve(url, wd_attr(n, "href"), b, sizeof b) == 0) wu_copy(p->doc->base, b, sizeof p->doc->base); break; }
    }
    p->css = wc_new(p->doc); if (!p->css) return -1;
    wc_add_sheet(p->css, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0);
    p->doc->sheets = p->css;
    prep_dom(p, 0);
    update_title(p);
    check_meta_refresh(p);
    return 0;
}

/* ------------------------------------------------------------ scripts */
static const char *const BLOCKED[] = {
    "google-analytics.com", "googletagmanager.com", "doubleclick.net", "googlesyndication.com", "adservice.google", "facebook.net", "connect.facebook",
    "hotjar.com", "scorecardresearch.com", "quantserve.com", "taboola.com", "outbrain.com", "criteo.", "amazon-adsystem", "clarity.ms", "segment.com",
    "mixpanel.com", "newrelic.com", "nr-data.net", "sentry.io", "optimizely.com", "addthis.com", "sharethis.com", "chartbeat", "krxd.net", "demdex.net",
    "pubmatic.com", "rubiconproject.com", "adnxs.com", "googleadservices.com", "ads-twitter.com", "analytics.twitter", "px.ads.linkedin", "bat.bing.com", 0
};
static int url_blocked(const char *u) { for (int i = 0; BLOCKED[i]; i++) if (wu_find(u, BLOCKED[i])) return 1; return 0; }
static int under_template(const struct wnode *n) { for (n = n->parent; n; n = n->parent) if (n->type == WN_ELEM && n->tag == WT_TEMPLATE) return 1; return 0; }
static struct wnode *next_script(struct wnode *root)
{
    struct wnode *n = root;
    while (n) {
        if (n->type == WN_ELEM && n->tag == WT_SCRIPT && !n->sel && !under_template(n)) return n;
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    return 0;
}
static int has_module_syntax(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i + 7 < n; i++) {
        if ((i == 0 || s[i - 1] == '\n' || s[i - 1] == ';' || s[i - 1] == '}') && (wu_strneq(s + i, "import ", 7) || wu_strneq(s + i, "import{", 7) || wu_strneq(s + i, "export ", 7))) return 1;
    }
    return 0;
}
static void run_scripts(struct wpage *p)
{
    if (!p->js) return;
    p->in_script = 1;
    for (int guard = 0; guard < 400 && !p->stop; guard++) {
        struct wnode *s = next_script(p->doc->root); if (!s) break;
        s->sel = 1;
        if (wjs_halted(p->js)) break;
        const char *ty = wd_attr(s, "type"); int module = 0;
        if (ty && *ty) {
            if (wu_strieq(ty, "module")) module = 1;
            else if (!(wu_strieq(ty, "text/javascript") || wu_strieq(ty, "application/javascript") || wu_strieq(ty, "application/x-javascript") || wu_strieq(ty, "text/ecmascript") || wu_strieq(ty, "application/ecmascript") || wu_strieq(ty, "text/jscript"))) continue;
        }
        const char *src = wd_attr(s, "src");
        if (module && src == 0 && wd_attr(s, "nomodule")) module = 0;
        if (src && *src) {
            if (now_ms() - p->load_t0 > WP_LOAD_BUDGET_MS) { wp_log(1, "script omitido: presupuesto de carga agotado"); continue; }
            char u[WN_URL_MAX]; abs_url(p, src, u, sizeof u);
            if (url_blocked(u)) continue;
            if (wd_attr(s, "nomodule") && 0) continue;
            struct wn_resp rs; set_status(p, WPS_BUSY, "Descargando script..."); p->phase = WP_SCRIPT;
            if (load_res(p, u, 0, 0, 0, 0, 0, WP_MAX_RES, 12000, &rs) == 0 && rs.body && rs.status >= 200 && rs.status < 300) {
                to_utf8(&rs, 0);
                if (module && has_module_syntax(rs.body, rs.len)) { wn_free(&rs); wjs_dispatch(p->js, s, "error", 0, 0, 0, 0, 0); continue; }
                set_status(p, WPS_BUSY, "Ejecutando scripts..."); wjs_set_script(p->js, s); wjs_eval(p->js, rs.body, rs.len, u); wjs_set_script(p->js, 0);
                wjs_dispatch(p->js, s, "load", 0, 0, 0, 0, 0);
            } else wjs_dispatch(p->js, s, "error", 0, 0, 0, 0, 0);
            wn_free(&rs);
        } else {
            if (module) continue;
            struct wb b = {0}; wd_text_content(s, &b);
            if (b.len && !(b.len > 20 && has_module_syntax(b.p, b.len) && module)) { wjs_set_script(p->js, s); wjs_eval(p->js, b.p, b.len, "script"); wjs_set_script(p->js, 0); }
            wb_free(&b);
        }
    }
    p->in_script = 0;
}
static void start_js(struct wpage *p)
{
    if (p->doc->nodes > 90000) { wp_log(1, "documento muy grande: JavaScript desactivado"); return; }
    p->js = wjs_new(p->doc, &p->host);
    if (!p->js) { wp_log(2, "no se pudo crear el motor JavaScript (memoria)"); return; }
    wjs_set_url(p->js, p->url);
}
static void fire_load(struct wpage *p)
{
    if (!p->js) return;
    struct wnode *b = p->doc->body; const char *ol = b ? wd_attr(b, "onload") : 0;
    if (ol && *ol) { struct wb c = {0}; wb_add(&c, "if(!window.onload)window.onload=function(event){"); wb_add(&c, ol); wb_add(&c, "\n};"); wjs_eval(p->js, c.p, c.len, "onload"); wb_free(&c); }
    wjs_fire_load(p->js);
}

/* ------------------------------------------------------------ paginas generadas */
static void page_error(struct wb *o, const char *title, const char *msg, const char *url, const char *hint)
{
    wb_add(o, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>"); html_escape(o, title, wu_strlen(title));
    wb_add(o, "</title><style>body{font-family:sans-serif;margin:48px;color:#333;background:#fafafa}h1{color:#b03a2e;font-size:26px}code{background:#e8e8e8;padding:2px 6px}p{font-size:15px;line-height:1.5}</style></head><body><h1>");
    html_escape(o, title, wu_strlen(title)); wb_add(o, "</h1><p>"); html_escape(o, msg, wu_strlen(msg)); wb_add(o, "</p><p><code>"); html_escape(o, url, wu_strlen(url)); wb_add(o, "</code></p>");
    if (hint && *hint) { wb_add(o, "<p>"); html_escape(o, hint, wu_strlen(hint)); wb_add(o, "</p>"); }
    wb_add(o, "</body></html>");
}
enum { K_HTML = 1, K_TEXT, K_IMAGE, K_BIN, K_SVG };
static int kind_of(const char *ct, const char *b, uint32_t n)
{
    if (wu_findi(ct, "svg")) return K_SVG;
    if (wu_findi(ct, "html")) return K_HTML;
    if (wu_startsi(ct, "image/")) return K_IMAGE;
    if (wu_startsi(ct, "text/") || wu_findi(ct, "json") || wu_findi(ct, "javascript") || wu_findi(ct, "xml") || wu_findi(ct, "css")) return K_TEXT;
    if (n >= 8 && (!memcmp(b, "\x89PNG", 4) || !memcmp(b, "GIF8", 4) || ((uint8_t)b[0] == 0xFF && (uint8_t)b[1] == 0xD8) || (b[0] == 'B' && b[1] == 'M'))) return K_IMAGE;
    uint32_t i = 0; while (i < n && wu_isspace(b[i])) i++;
    if (i < n && b[i] == '<') return K_HTML;
    for (uint32_t k = 0; k < (n < 512 ? n : 512); k++) if (b[k] == 0) return K_BIN;
    return ct[0] == 0 || wu_findi(ct, "text") ? K_HTML : K_TEXT;
}

/* ------------------------------------------------------------ navegacion */
static void anchor_scroll(struct wpage *p, const char *frag)
{
    if (!p->doc || !p->lay) return;
    if (!frag || !*frag) { p->sy = 0; return; }
    char id[200]; uint32_t k = 0;
    for (const char *s = frag; *s && k + 1 < sizeof id; s++) { if (*s == '%' && wu_ishex(s[1]) && wu_ishex(s[2])) { id[k++] = (char)(wu_hexval(s[1]) * 16 + wu_hexval(s[2])); s += 2; } else id[k++] = *s; }
    id[k] = 0;
    struct wnode *t = wd_by_id(p->doc->root, id);
    if (!t) { for (struct wnode *n = p->doc->root; n; ) { if (n->type == WN_ELEM && n->tag == WT_A && wd_attr(n, "name") && wu_streq(wd_attr(n, "name"), id)) { t = n; break; } if (n->first) { n = n->first; continue; } while (n && !n->next) n = n->parent; if (n) n = n->next; } }
    if (!t) { if (wu_strieq(id, "top")) p->sy = 0; return; }
    int x, y, w, h; if (wl_node_rect(p->lay, t, &x, &y, &w, &h)) p->sy = y > 8 ? y - 8 : 0;
}
static void hist_push(struct wpage *p, const char *url)
{
    if (p->hpos >= 0 && p->hpos < p->hn) p->hn = p->hpos + 1; else p->hn = 0;
    if (p->hn >= WP_HIST) { memmove(&p->hist[0], &p->hist[1], sizeof p->hist[0] * (WP_HIST - 1)); p->hn = WP_HIST - 1; }
    struct wp_hist *h = &p->hist[p->hn]; wu_copy(h->url, url, sizeof h->url); h->scroll_y = 0; h->title[0] = 0; p->hpos = p->hn; p->hn++;
}
static void same_doc_nav(struct wpage *p, const char *url, int replace)
{
    if (p->hpos >= 0 && p->hpos < p->hn) p->hist[p->hpos].scroll_y = p->sy;
    char old[WN_URL_MAX]; wu_copy(old, p->url, sizeof old);
    wu_copy(p->url, url, sizeof p->url);
    if (replace && p->hpos >= 0 && p->hpos < p->hn) wu_copy(p->hist[p->hpos].url, url, sizeof p->hist[0].url); else hist_push(p, url);
    if (p->js) wjs_set_url(p->js, p->url);
    ensure_layout(p);
    const char *h = wu_strchr(url, '#'); anchor_scroll(p, h ? h + 1 : "");
    if (p->js && !wu_streq(old, url)) wjs_eval(p->js, "window.dispatchEvent(new HashChangeEvent('hashchange'));", 52, "hashchange");
}

static int nav_core(struct wpage *p, const char *url_in, int flags, const char *post, uint32_t plen, const char *pctype)
{
    char url[WN_URL_MAX]; int implicit = 0; norm_url(url_in, url, sizeof url, &implicit);
    if (!url[0]) return -1;
    if (wu_startsi(url, "javascript:")) { char o[200]; wp_eval_js(p, url + 11, o, sizeof o); return 0; }
    if (url[0] == '#' || url[0] == '?') { char t[WN_URL_MAX]; abs_url(p, url, t, sizeof t); wu_copy(url, t, sizeof url); }
    if (wu_startsi(url, "about:blank") || wu_streq(url, "about:")) {
        static const char blank[] = "<!DOCTYPE html><html><head><title>about:blank</title></head><body></body></html>";
        if (build_document(p, blank, sizeof blank - 1, "about:blank") != 0) return -1;
        wu_copy(p->url, "about:blank", sizeof p->url); relayout(p, 0);
        if (!(flags & WPN_NOHIST)) hist_push(p, p->url);
        set_status(p, WPS_OK, "Listo"); p->phase = WP_IDLE; return 0;
    }
    if (p->doc && !post && !(flags & WPN_RELOAD) && wu_strchr(url, '#')) {
        const char *a = p->url, *b = url; uint32_t la = 0, lb = 0; while (a[la] && a[la] != '#') la++; while (b[lb] && b[lb] != '#') lb++;
        if (la == lb && wu_strneq(a, b, la)) { same_doc_nav(p, url, (flags & WPN_REPLACE) != 0); return 0; }
    }
    p->stop = 0; p->load_t0 = now_ms(); p->phase = WP_FETCH; p->progress = 5;
    if (!(flags & WPN_NOHIST) && p->hpos >= 0 && p->hpos < p->hn) p->hist[p->hpos].scroll_y = p->sy;
    int saved_scroll = (flags & WPN_NOHIST) && p->hpos >= 0 && p->hpos < p->hn ? p->hist[p->hpos].scroll_y : 0;
    set_status(p, WPS_BUSY, wu_startsi(url, "file:") ? "Leyendo archivo..." : "Conectando...");
    struct wn_resp rs; int rc = load_res(p, url, post ? "POST" : 0, post, plen, pctype, 0, WP_MAX_HTML, 20000, &rs);
    if (rc != 0 && implicit && !post && wu_startsi(url, "https://") == 1) {          /* https implicito sin pasarela: se prueba http */
        char alt[WN_URL_MAX]; wu_copy(alt, "http://", sizeof alt); wu_copy(alt + 7, url + 8, sizeof alt - 7);
        wn_free(&rs); rc = load_res(p, alt, 0, 0, 0, 0, 0, WP_MAX_HTML, 20000, &rs); if (rc == 0) wu_copy(url, alt, sizeof url);
    }
    char final_url[WN_URL_MAX]; wu_copy(final_url, rs.final_url[0] ? rs.final_url : url, sizeof final_url);
    struct wb page = {0}; const char *html = 0; uint32_t hlen = 0; int seed_img = 0; int err = 0;
    if (rc != 0 || !rs.body) {
        const char *hint = wu_startsi(url, "https://") ? "Las paginas HTTPS pasan por la pasarela del equipo anfitrion (puerto 8096). ejecutar.py la arranca sola; si lanzas QEMU a mano, inicia antes tools/web_gateway.py." : "Comprueba la direccion y la conexion de red.";
        page_error(&page, "No se pudo cargar la pagina", rs.err[0] ? rs.err : "error de red", url, hint);
        html = page.p; hlen = page.len; err = 1; set_status(p, WPS_ERR, rs.err[0] ? rs.err : "Error de red"); wu_copy(final_url, url, sizeof final_url);
    } else {
        int kind = kind_of(rs.ctype, rs.body, rs.len);
        if (kind == K_HTML) {
            to_utf8(&rs, 1); html = rs.body; hlen = rs.len;
            if (hlen == 0 && rs.status >= 400) { page_error(&page, "Error HTTP", "El servidor no devolvio contenido.", url, 0); html = page.p; hlen = page.len; }
        } else if (kind == K_TEXT) {
            to_utf8(&rs, 0); wb_add(&page, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>"); const char *nm = final_url; const char *sl = nm; while (*sl) { if (*sl == '/') nm = sl + 1; sl++; } html_escape(&page, nm, wu_strlen(nm));
            wb_add(&page, "</title></head><body style=\"margin:8px\"><pre style=\"white-space:pre-wrap;font-family:monospace;font-size:13px\">"); html_escape(&page, rs.body, rs.len > (1u << 20) ? (1u << 20) : rs.len); wb_add(&page, "</pre></body></html>");
            html = page.p; hlen = page.len;
        } else if (kind == K_IMAGE) {
            wb_add(&page, "<!DOCTYPE html><html><head><title>"); html_escape(&page, final_url, wu_strlen(final_url)); wb_add(&page, "</title></head><body style=\"margin:0;background:#1c1c1c;text-align:center\"><img src=\""); html_escape(&page, final_url, wu_strlen(final_url)); wb_add(&page, "\" style=\"max-width:100%\"></body></html>");
            html = page.p; hlen = page.len; seed_img = 1;
        } else {
            page_error(&page, kind == K_SVG ? "Imagen SVG" : "Contenido no soportado", kind == K_SVG ? "Este navegador todavia no dibuja graficos SVG." : "El tipo de contenido no se puede mostrar en el navegador.", final_url, rs.ctype);
            html = page.p; hlen = page.len;
        }
        if (rs.status >= 400) { char m[64] = "HTTP "; wu_itoa(rs.status, m + 5); set_status(p, WPS_ERR, m); } else set_status(p, WPS_BUSY, "Procesando pagina...");
    }
    if (!html) { wn_free(&rs); wb_free(&page); return -1; }
    p->progress = 30;
    if (build_document(p, html, hlen, final_url) != 0) { wn_free(&rs); wb_free(&page); set_status(p, WPS_ERR, "Sin memoria"); p->phase = WP_IDLE; return -1; }
    if (seed_img) img_seed(p, final_url, rs.body, rs.len);
    int http_status = rs.status;
    wn_free(&rs); wb_free(&page);
    wu_copy(p->url, final_url, sizeof p->url);
    if (!(flags & WPN_NOHIST)) { if ((flags & (WPN_REPLACE | WPN_RELOAD)) && p->hpos >= 0 && p->hpos < p->hn) { wu_copy(p->hist[p->hpos].url, final_url, sizeof p->hist[0].url); } else hist_push(p, final_url); }
    p->sx = 0; p->sy = 0;
    /* estilos + primera maquetacion (se puede ver la pagina antes de ejecutar scripts) */
    p->phase = WP_RES; p->progress = 45; if (!err && http_status < 400) set_status(p, WPS_BUSY, "Cargando estilos...");
    relayout(p, 1);
    p->progress = 60;
    if (!err && !p->stop) {
        start_js(p);
        if (p->js) { p->phase = WP_SCRIPT; set_status(p, WPS_BUSY, "Ejecutando scripts..."); run_scripts(p); p->progress = 80; }
        if (p->stop == 0) { relayout(p, 1); fire_load(p); }
        p->forced = 0; relayout(p, 1);
    }
    update_title(p);
    if (saved_scroll) p->sy = saved_scroll;
    else { const char *h = wu_strchr(final_url, '#'); if (h && h[1]) anchor_scroll(p, h + 1); }
    if (p->lay) { int mx = p->lay->doc_h - p->vh; if (mx < 0) mx = 0; if (p->sy > mx) p->sy = mx; }
    if (http_status >= 400) { char m[64] = "HTTP "; wu_itoa(http_status, m + 5); set_status(p, WPS_ERR, m); }
    else if (!err) { char m[64] = "Listo ("; uint32_t ms = (uint32_t)(now_ms() - p->load_t0); int k = 7; k += wu_itoa(ms / 1000, m + k); m[k++] = '.'; m[k++] = (char)('0' + (ms % 1000) / 100); m[k++] = ' '; m[k++] = 's'; m[k++] = ')'; m[k] = 0; set_status(p, WPS_OK, m); }
    p->progress = 100; p->phase = p->img_pending > 0 ? WP_IMAGES : WP_IDLE;
    return err ? -1 : 0;
}
int wp_navigate(struct wpage *p, const char *url, int flags) { return nav_core(p, url, flags, 0, 0, 0); }
int wp_post(struct wpage *p, const char *url, const char *body, uint32_t blen, const char *ctype) { return nav_core(p, url, 0, body, blen, ctype); }
void wp_history_go(struct wpage *p, int delta)
{
    int t = p->hpos + delta; if (t < 0 || t >= p->hn) return;
    if (p->hpos >= 0 && p->hpos < p->hn) p->hist[p->hpos].scroll_y = p->sy;
    p->hpos = t; char u[WN_URL_MAX]; wu_copy(u, p->hist[t].url, sizeof u);
    nav_core(p, u, WPN_NOHIST, 0, 0, 0);
}
int wp_can_back(const struct wpage *p) { return p->hpos > 0; }
int wp_can_fwd(const struct wpage *p) { return p->hpos >= 0 && p->hpos + 1 < p->hn; }
void wp_stop(struct wpage *p) { p->stop = 1; }
static void flush_nav(struct wpage *p)
{
    if (!p->nav_pending) return;
    p->nav_pending = 0; char u[WN_URL_MAX]; wu_copy(u, p->nav_url, sizeof u); int mode = p->nav_mode;
    if (mode == 5) { wp_history_go(p, p->nav_delta); return; }
    if (mode == 6) { char *post = p->nav_post; uint32_t pl = p->nav_plen; p->nav_post = 0; char ct[64]; wu_copy(ct, p->nav_ctype, sizeof ct); if (post) { nav_core(p, u, 0, post, pl, ct); kfree(post); } return; }
    nav_core(p, u, mode == 1 ? WPN_REPLACE : mode == 4 ? WPN_RELOAD : 0, 0, 0, 0);
}

/* ------------------------------------------------------------ ciclo de vida */
struct wpage *wp_new(void)
{
    struct wpage *p = (struct wpage *)kmalloc(sizeof *p); if (!p) return 0;
    memset(p, 0, sizeof *p); host_init(p); p->hpos = -1; p->vw = 800; p->vh = 560;
    set_status(p, WPS_OK, "Listo"); return p;
}
void wp_free(struct wpage *p)
{
    if (!p) return;
    reset_doc(p);
    if (p->nav_post) kfree(p->nav_post);
    if (p->ss) { store_clear(p->ss); kfree(p->ss); }
    kfree(p);
}
void wp_set_viewport(struct wpage *p, int vw, int vh)
{
    if (vw < 16) vw = 16;
    if (vh < 16) vh = 16;
    if (vw != p->vw || vh != p->vh) { p->vw = vw; p->vh = vh; p->need_style = 1; p->resized = 1; }
}
const char *wp_status(const struct wpage *p) { return p->status; }
void wp_paint(struct wpage *p, int sx, int sy, int sw, int sh) { if (p->lay) wl_paint(p->lay, sx, sy, sw, sh, p->sx, p->sy); }
int wp_doc_height(const struct wpage *p) { return p->lay ? p->lay->doc_h : 0; }
void wp_scroll_by(struct wpage *p, int dx, int dy)
{
    int mx = p->lay ? p->lay->doc_h - p->vh : 0, mxx = p->lay ? p->lay->doc_w - p->vw : 0; if (mx < 0) mx = 0; if (mxx < 0) mxx = 0;
    p->sy += dy; p->sx += dx; if (p->sy < 0) p->sy = 0; if (p->sy > mx) p->sy = mx; if (p->sx < 0) p->sx = 0; if (p->sx > mxx) p->sx = mxx;
}

/* ------------------------------------------------------------ eventos */
static int fire(struct wpage *p, struct wnode *t, const char *type, int x, int y, int key, const char *ks, int mods)
{
    if (!p->js || wjs_halted(p->js)) return 0;
    return wjs_dispatch(p->js, t, type, x, y, key, ks, mods);
}
static int is_text_ctl(const struct wnode *n)
{
    if (!n || n->type != WN_ELEM) return 0;
    if (n->tag == WT_TEXTAREA) return 1;
    if (n->tag != WT_INPUT) return 0;
    const char *t = wd_attr(n, "type"); if (!t || !*t) return 1;
    static const char *const NT[] = { "checkbox", "radio", "submit", "button", "reset", "image", "file", "range", "color", "hidden", 0 };
    for (int i = 0; NT[i]; i++) if (wu_strieq(t, NT[i])) return 0;
    return 1;
}
static struct wnode *focusable(struct wnode *n)
{
    for (; n; n = n->parent) {
        if (n->type != WN_ELEM) continue;
        if (ctl_disabled(n)) return 0;
        switch (n->tag) {
        case WT_INPUT: { const char *t = wd_attr(n, "type"); if (t && wu_strieq(t, "hidden")) return 0; return n; }
        case WT_TEXTAREA: case WT_SELECT: case WT_BUTTON: return n;
        case WT_A: if (wd_attr(n, "href")) return n; break;
        default: break;
        }
        const char *ti = wd_attr(n, "tabindex"); if (ti && wu_atoi(ti) >= 0) return n;
    }
    return 0;
}
static void blur_focus(struct wpage *p) { (void)p; }
static void set_focus(struct wpage *p, struct wnode *n)
{
    if (p->focus == n) return;
    struct wnode *old = p->focus;
    if (old) {
        const char *cur = old->value ? old->value : wd_attr(old, "value");
        if (is_text_ctl(old) && cur != p->focus_orig && (old->flags & WNF_EDITED)) fire(p, old, "change", 0, 0, 0, 0, 0);
        old->flags &= (uint16_t)~WNF_FOCUS; fire(p, old, "blur", 0, 0, 0, 0, 0); fire(p, old, "focusout", 0, 0, 0, 0, 0);
    }
    p->focus = n; p->focus_orig = 0;
    if (n) {
        n->flags |= WNF_FOCUS; p->focus_orig = n->value ? n->value : wd_attr(n, "value");
        if (is_text_ctl(n) && !(n->flags & WNF_EDITED)) n->sel = (int32_t)wu_strlen(wl_ctl_text(n));
        fire(p, n, "focus", 0, 0, 0, 0, 0); fire(p, n, "focusin", 0, 0, 0, 0, 0);
    }
    if (p->doc) p->doc->focus = n;
    if (p->lay) p->lay->focus = n;
    p->caret_on = 1; p->caret_t = now_ms();
    if (p->lay) p->lay->caret_on = 1;
}
static void scroll_into_view(struct wpage *p, struct wnode *n)
{
    int x, y, w, h; if (!p->lay || !wl_node_rect(p->lay, n, &x, &y, &w, &h)) return;
    if (y < p->sy + 8) p->sy = y > 40 ? y - 40 : 0; else if (y + h > p->sy + p->vh - 8) p->sy = y + h - p->vh + 40;
    wp_scroll_by(p, 0, 0);
}
static int caret_from_click(struct wpage *p, struct wnode *n, int px, int py)
{
    const char *t = wl_ctl_text(n); int x, y, w, h; uint32_t len = wu_strlen(t);
    if (!p->lay || !wl_node_rect(p->lay, n, &x, &y, &w, &h)) return (int)len;
    const struct wstyle *s = n->st; int fpx = s && s->font_px ? s->font_px : 14; int fl = (s && s->bold ? WF_BOLD : 0) | (s && s->mono ? WF_MONO : 0);
    int lh = fpx + fpx / 4 + 2, line = 0; uint32_t start = 0;
    if (n->tag == WT_TEXTAREA) {
        line = (py - y - 3) / (lh > 0 ? lh : 16); if (line < 0) line = 0;
        for (uint32_t i = 0; i < len && line > 0; i++) if (t[i] == '\n') { start = i + 1; line--; }
    }
    int rel = (px - x - 5) * 64, acc = 0; uint32_t i = start;
    while (i < len && t[i] != '\n') {
        uint32_t adv; uint32_t cp = wu_utf8_next(t + i, &adv); int cw = wf_cp_adv64(cp, fpx, fl);
        if (acc + cw / 2 > rel) break;
        acc += cw; i += adv ? adv : 1;
    }
    return (int)i;
}
static void set_value(struct wpage *p, struct wnode *n, const char *s, uint32_t len, int caret)
{
    char *v = wa_strndup(&p->doc->ar, s, len); if (!v) return;
    n->value = v; n->flags |= WNF_EDITED; n->sel = caret;
    p->caret_on = 1; p->caret_t = now_ms();
}
static void radio_select(struct wpage *p, struct wnode *r)
{
    const char *nm = wd_attr(r, "name"); struct wnode *form = enclosing_form(r);
    if (nm && *nm) for (struct wnode *n = p->doc->root; n; ) {
        if (n->type == WN_ELEM && n->tag == WT_INPUT && n != r) { const char *t = wd_attr(n, "type"); const char *m = wd_attr(n, "name"); if (t && wu_strieq(t, "radio") && m && wu_streq(m, nm) && enclosing_form(n) == form) n->flags &= (uint16_t)~WNF_CHECKED; }
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    r->flags |= WNF_CHECKED;
}
static void do_submit(struct wpage *p, struct wnode *form, struct wnode *submitter)
{
    if (!form) return;
    if (fire(p, form, "submit", 0, 0, 0, 0, 0)) return;
    form_submit(p, form, submitter);
}
static struct wnode *default_button(struct wnode *form)
{
    for (struct wnode *n = form->first; n; ) {
        if (n->type == WN_ELEM && !ctl_disabled(n)) {
            if (n->tag == WT_BUTTON) { const char *t = wd_attr(n, "type"); if (!t || wu_strieq(t, "submit")) return n; }
            if (n->tag == WT_INPUT) { const char *t = wd_attr(n, "type"); if (t && (wu_strieq(t, "submit") || wu_strieq(t, "image"))) return n; }
        }
        if (n->first) { n = n->first; continue; }
        while (n && n != form && !n->next) n = n->parent;
        if (!n || n == form) break;
        n = n->next;
    }
    return 0;
}
static void follow_link(struct wpage *p, struct wnode *a)
{
    const char *href = wd_attr(a, "href"); if (!href) return;
    while (wu_isspace(*href)) href++;
    if (wu_startsi(href, "javascript:")) { char o[160]; wp_eval_js(p, href + 11, o, sizeof o); return; }
    if (wu_startsi(href, "mailto:") || wu_startsi(href, "tel:") || wu_startsi(href, "sms:")) { set_status(p, WPS_ERR, "Enlace de correo/telefono: no soportado"); return; }
    char u[WN_URL_MAX]; abs_url(p, href, u, sizeof u);
    host_navigate(p, u, 0);
}
static void activate_node(struct wpage *p, struct wnode *n, int px, int py)
{
    for (struct wnode *a = n; a; a = a->parent) {
        if (a->type != WN_ELEM) continue;
        switch (a->tag) {
        case WT_A: if (wd_attr(a, "href")) { follow_link(p, a); return; } break;
        case WT_BUTTON: {
            if (ctl_disabled(a)) return;
            const char *t = wd_attr(a, "type"); struct wnode *f = enclosing_form(a);
            if (!t || wu_strieq(t, "submit") || !*t) do_submit(p, f, a); else if (wu_strieq(t, "reset") && f) form_reset(p, f);
            return; }
        case WT_INPUT: {
            if (ctl_disabled(a)) return;
            const char *t = wd_attr(a, "type"); char tl[16]; wu_copy(tl, t ? t : "text", sizeof tl); for (char *c = tl; *c; c++) *c = (char)wu_lower(*c);
            if (wu_streq(tl, "checkbox")) { a->flags ^= WNF_CHECKED; fire(p, a, "input", 0, 0, 0, 0, 0); fire(p, a, "change", 0, 0, 0, 0, 0); }
            else if (wu_streq(tl, "radio")) { if (!(a->flags & WNF_CHECKED)) { radio_select(p, a); fire(p, a, "input", 0, 0, 0, 0, 0); fire(p, a, "change", 0, 0, 0, 0, 0); } }
            else if (wu_streq(tl, "submit") || wu_streq(tl, "image")) do_submit(p, enclosing_form(a), a);
            else if (wu_streq(tl, "reset")) { struct wnode *f = enclosing_form(a); if (f) form_reset(p, f); }
            else if (wu_streq(tl, "button") || wu_streq(tl, "hidden") || wu_streq(tl, "color")) { }
            else if (wu_streq(tl, "file")) set_status(p, WPS_ERR, "La seleccion de archivos no esta soportada");
            else if (wu_streq(tl, "range")) {
                int x, y, w, h; if (p->lay && px && wl_node_rect(p->lay, a, &x, &y, &w, &h) && w > 8) {
                    const char *mn = wd_attr(a, "min"), *mx = wd_attr(a, "max"); int lo = mn ? wu_atoi(mn) : 0, hi = mx ? wu_atoi(mx) : 100; int v = lo + (px - x) * (hi - lo) / w; if (v < lo) v = lo; if (v > hi) v = hi;
                    char b[24]; int k = wu_itoa(v, b); set_value(p, a, b, (uint32_t)k, 0); fire(p, a, "input", 0, 0, 0, 0, 0); fire(p, a, "change", 0, 0, 0, 0, 0);
                } }
            else { set_focus(p, a); if (px) { a->sel = caret_from_click(p, a, px, py); a->flags |= WNF_EDITED; p->caret_on = 1; } }
            return; }
        case WT_TEXTAREA: if (!ctl_disabled(a)) { set_focus(p, a); if (px) { a->sel = caret_from_click(p, a, px, py); a->flags |= WNF_EDITED; } } return;
        case WT_SELECT: if (!ctl_disabled(a)) { set_focus(p, a); p->dd_sel = a; } return;
        case WT_LABEL: {
            const char *f = wd_attr(a, "for"); struct wnode *t = 0;
            if (f && *f) t = wd_by_id(p->doc->root, f);
            else for (struct wnode *c = a->first; c && !t; ) { if (c->type == WN_ELEM && (c->tag == WT_INPUT || c->tag == WT_SELECT || c->tag == WT_TEXTAREA || c->tag == WT_BUTTON)) t = c; else if (c->first) { c = c->first; continue; } while (c && c != a && !c->next) c = c->parent; if (!c || c == a) break; c = c->next; }
            if (t && t != n) activate_node(p, t, 0, 0);
            return; }
        case WT_SUMMARY: if (a->parent && a->parent->tag == WT_DETAILS) { a->parent->flags ^= WNF_OPEN; p->need_style = 1; return; } break;
        default: break;
        }
    }
}
void wp_select_choose(struct wpage *p, struct wnode *sel, int index)
{
    p->dd_sel = 0; if (!sel || index < 0 || index >= wl_select_count(sel)) return;
    int changed = wl_select_index(sel) != index;
    sel->sel = index; sel->flags |= WNF_EDITED;
    if (changed) { fire(p, sel, "input", 0, 0, 0, 0, 0); fire(p, sel, "change", 0, 0, 0, 0, 0); }
    flush_nav(p);
}
void wp_mouse_click(struct wpage *p, int px, int py, int button, int mods, int dbl)
{
    if (!p->lay || p->dead) return;
    p->forced = 0; ensure_layout(p);
    struct wnode *link; struct wnode *n = wl_hit(p->lay, px, py, &link, 0); int cx = px - p->sx, cy = py - p->sy;
    if (!n) { set_focus(p, 0); return; }
    p->dd_sel = 0;
    fire(p, n, "mousedown", cx, cy, button, 0, mods);
    struct wnode *f = focusable(n);
    if (f && is_text_ctl(f) == 0 && (f->tag == WT_INPUT || f->tag == WT_A)) { /* los botones y casillas toman el foco al activarse */ }
    set_focus(p, f);
    fire(p, n, "mouseup", cx, cy, button, 0, mods);
    int prevented = fire(p, n, button == 2 ? "contextmenu" : button == 1 ? "auxclick" : "click", cx, cy, button, 0, mods);
    if (!prevented && button == 0) { activate_node(p, n, px, py); if (dbl) fire(p, n, "dblclick", cx, cy, 0, 0, mods); }
    flush_nav(p);
}
void wp_mouse_move(struct wpage *p, int px, int py)
{
    if (!p->lay || p->dead) return;
    struct wnode *n = wl_hit(p->lay, px, py, 0, 0);
    if (n == p->hover) return;
    struct wnode *old = p->hover;
    for (struct wnode *a = old; a; a = a->parent) a->flags &= (uint16_t)~WNF_HOVER;
    for (struct wnode *a = n; a; a = a->parent) a->flags |= WNF_HOVER;
    p->hover = n; p->lay->hover = n;
    if (p->js && !p->in_script) { if (old) fire(p, old, "mouseout", px - p->sx, py - p->sy, 0, 0, 0); if (n) fire(p, n, "mouseover", px - p->sx, py - p->sy, 0, 0, 0); }
    if (p->hover_rules) p->need_style = 1;
}
int wp_hit_cursor(struct wpage *p, int px, int py)
{
    if (!p->lay || p->dead) return WPK_NONE;
    struct wnode *link; struct wnode *n = wl_hit(p->lay, px, py, &link, 0);
    if (!n) return WPK_NONE;
    if (link) return WPK_LINK;
    for (struct wnode *a = n; a; a = a->parent) {
        if (a->type != WN_ELEM) continue;
        if (is_text_ctl(a)) return WPK_TEXT;
        if (a->tag == WT_BUTTON || a->tag == WT_SELECT || a->tag == WT_LABEL || a->tag == WT_SUMMARY || (a->tag == WT_INPUT && !is_text_ctl(a))) return WPK_LINK;
        if (a->st && a->st->pointer) return WPK_LINK;
    }
    return WPK_NONE;
}

/* ------------------------------------------------------------ teclado */
static void key_info(int key, const char *utf8, char *name, uint32_t max, int *kc)
{
    *kc = 0; name[0] = 0;
    switch (key) {
    case 8: case 127: wu_copy(name, "Backspace", max); *kc = 8; return;
    case 9: wu_copy(name, "Tab", max); *kc = 9; return;
    case 10: case 13: wu_copy(name, "Enter", max); *kc = 13; return;
    case 16: wu_copy(name, "ArrowUp", max); *kc = 38; return;
    case 14: wu_copy(name, "ArrowDown", max); *kc = 40; return;
    case 2: wu_copy(name, "ArrowLeft", max); *kc = 37; return;
    case 6: wu_copy(name, "ArrowRight", max); *kc = 39; return;
    case 1: wu_copy(name, "Home", max); *kc = 36; return;
    case 5: wu_copy(name, "End", max); *kc = 35; return;
    case 28: wu_copy(name, "PageUp", max); *kc = 33; return;
    case 29: wu_copy(name, "PageDown", max); *kc = 34; return;
    case 4: wu_copy(name, "Delete", max); *kc = 46; return;
    case 27: wu_copy(name, "Escape", max); *kc = 27; return;
    default: break;
    }
    if (utf8 && utf8[0]) { wu_copy(name, utf8, max); unsigned char c = (unsigned char)utf8[0]; if (c < 128) *kc = (c >= 'a' && c <= 'z') ? c - 32 : c; }
}
static int utf8_prev(const char *s, int i) { if (i <= 0) return 0; i--; while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--; return i; }
static int utf8_next(const char *s, int len, int i) { if (i >= len) return len; i++; while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80) i++; return i; }
static int line_start(const char *s, int i) { while (i > 0 && s[i - 1] != '\n') i--; return i; }
static int line_end(const char *s, int len, int i) { while (i < len && s[i] != '\n') i++; return i; }
/* devuelve 1 si la tecla se consumio */
static int text_edit(struct wpage *p, struct wnode *n, int key, const char *utf8)
{
    if (wd_attr(n, "readonly")) return key == 13 || key == 10 ? 0 : (key == 16 || key == 14 ? 0 : 1);
    const char *cur = wl_ctl_text(n); int len = (int)wu_strlen(cur); int c = n->sel < 0 ? 0 : n->sel > len ? len : n->sel; int multi = n->tag == WT_TEXTAREA;
    char *buf = (char *)kmalloc((uint32_t)len + 8 + (utf8 ? wu_strlen(utf8) : 0)); if (!buf) return 1;
    memcpy(buf, cur, (uint32_t)len + 1);
    int edit = 0, data = 0; int nc = c;
    if (key == 8 || key == 127) { if (c > 0) { int pc = utf8_prev(buf, c); memmove(buf + pc, buf + c, (uint32_t)(len - c + 1)); len -= c - pc; nc = pc; edit = 1; } }
    else if (key == 4) { if (c < len) { int nx = utf8_next(buf, len, c); memmove(buf + c, buf + nx, (uint32_t)(len - nx + 1)); len -= nx - c; edit = 1; } }
    else if (key == 2) { nc = utf8_prev(buf, c); n->sel = nc; p->caret_on = 1; p->caret_t = now_ms(); kfree(buf); return 1; }
    else if (key == 6) { nc = utf8_next(buf, len, c); n->sel = nc; p->caret_on = 1; p->caret_t = now_ms(); kfree(buf); return 1; }
    else if (key == 1) { n->sel = multi ? line_start(buf, c) : 0; kfree(buf); return 1; }
    else if (key == 5) { n->sel = multi ? line_end(buf, len, c) : len; kfree(buf); return 1; }
    else if (key == 16 || key == 14) {
        if (!multi) { kfree(buf); return 0; }
        int ls = line_start(buf, c), col = c - ls;
        if (key == 16) { if (ls > 0) { int ps = line_start(buf, ls - 1); int pl = ls - 1 - ps; n->sel = ps + (col < pl ? col : pl); } }
        else { int le = line_end(buf, len, c); if (le < len) { int ns = le + 1, ne = line_end(buf, len, ns); n->sel = ns + (col < ne - ns ? col : ne - ns); } }
        kfree(buf); return 1;
    }
    else if (key == 13 || key == 10) {
        if (multi) { memmove(buf + c + 1, buf + c, (uint32_t)(len - c + 1)); buf[c] = '\n'; len++; nc = c + 1; edit = 1; data = 1; }
        else { kfree(buf); return 0; }
    }
    else if (utf8 && utf8[0] && (unsigned char)utf8[0] >= 32 && key != 27 && key != 9) {
        uint32_t ul = wu_strlen(utf8);
        const char *ml = wd_attr(n, "maxlength");
        if (ml && wu_atoi(ml) > 0) { int chars = 0; for (int i = 0; i < len; i = utf8_next(buf, len, i)) chars++; if (chars >= wu_atoi(ml)) { kfree(buf); return 1; } }
        const char *ty = wd_attr(n, "type");
        if (ty && wu_strieq(ty, "number") && !(wu_isdigit(utf8[0]) || utf8[0] == '-' || utf8[0] == '+' || utf8[0] == '.' || utf8[0] == 'e' || utf8[0] == 'E')) { kfree(buf); return 1; }
        if (len + (int)ul > 200000) { kfree(buf); return 1; }
        memmove(buf + c + ul, buf + c, (uint32_t)(len - c + 1)); memcpy(buf + c, utf8, ul); len += (int)ul; nc = c + (int)ul; edit = 1; data = 1;
    } else { kfree(buf); return 0; }
    if (edit) { set_value(p, n, buf, (uint32_t)len, nc); fire(p, n, "input", 0, 0, 0, data && utf8 ? utf8 : 0, 0); }
    kfree(buf); return 1;
}
static int tab_stop(const struct wnode *n)
{
    if (n->type != WN_ELEM || ctl_disabled(n) || !n->st || n->st->display == D_NONE || n->st->visibility) return 0;
    if (n->tag == WT_A) return wd_attr(n, "href") != 0 && n->box != 0;
    if (n->tag == WT_INPUT) { const char *t = wd_attr(n, "type"); if (t && wu_strieq(t, "hidden")) return 0; return n->box != 0; }
    if (n->tag == WT_BUTTON || n->tag == WT_SELECT || n->tag == WT_TEXTAREA) return n->box != 0;
    const char *ti = wd_attr(n, "tabindex"); return ti && wu_atoi(ti) >= 0 && n->box != 0;
}
static void focus_move(struct wpage *p, int dir)
{
    struct wnode *first = 0, *last = 0, *prev = 0, *found = 0; int seen = 0;
    for (struct wnode *n = p->doc->root; n; ) {
        if (tab_stop(n)) {
            if (!first) first = n;
            last = n;
            if (p->focus == n) seen = 1; else if (seen && !found && dir > 0) found = n;
            if (!seen) prev = n;
        }
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    struct wnode *t = dir > 0 ? (found ? found : first) : (prev ? prev : last);
    if (t) { set_focus(p, t); scroll_into_view(p, t); }
}
void wp_key(struct wpage *p, int key, const char *utf8, int mods)
{
    if (!p->lay || p->dead) return;
    p->forced = 0; ensure_layout(p);
    char kn[24]; int kc; key_info(key, utf8, kn, sizeof kn, &kc);
    struct wnode *target = p->focus ? p->focus : p->doc->body ? p->doc->body : p->doc->root;
    int pd = fire(p, target, "keydown", 0, 0, kc, kn, mods);
    int printable = utf8 && utf8[0] && (unsigned char)utf8[0] >= 32 && key != 127;
    if (!pd) {
        struct wnode *f = p->focus; int done = 0;
        if (key == 9) { focus_move(p, (mods & 1) ? -1 : 1); done = 1; }
        else if (f && is_text_ctl(f)) {
            done = text_edit(p, f, key, utf8);
            if (!done && (key == 13 || key == 10) && f->tag == WT_INPUT) {
                fire(p, f, "change", 0, 0, 0, 0, 0); struct wnode *form = enclosing_form(f);
                if (form) do_submit(p, form, default_button(form));
                done = 1;
            }
        } else if (f && (key == 13 || key == 10 || key == ' ')) {
            int chk = f->tag == WT_INPUT && wd_attr(f, "type") && (wu_strieq(wd_attr(f, "type"), "checkbox") || wu_strieq(wd_attr(f, "type"), "radio"));
            if (f->tag == WT_A) { if (key != ' ') { activate_node(p, f, 0, 0); done = 1; } }
            else if (chk) { if (key == ' ') { activate_node(p, f, 0, 0); } done = 1; }
            else { activate_node(p, f, 0, 0); done = 1; }
        } else if (f && f->tag == WT_SELECT && (key == 16 || key == 14)) {
            int cnt = wl_select_count(f), cur = wl_select_index(f), t = cur + (key == 14 ? 1 : -1); if (t >= 0 && t < cnt) wp_select_choose(p, f, t); done = 1;
        }
        if (!done) {
            int line = 40, page = p->vh > 120 ? p->vh - 40 : p->vh;
            if (key == 16) wp_scroll_by(p, 0, -line); else if (key == 14) wp_scroll_by(p, 0, line);
            else if (key == 28) wp_scroll_by(p, 0, -page); else if (key == 29 || key == ' ') wp_scroll_by(p, 0, (mods & 1) ? -page : page);
            else if (key == 1) p->sy = 0; else if (key == 5) wp_scroll_by(p, 0, 1 << 24);
            else if (key == 2) wp_scroll_by(p, -line, 0); else if (key == 6) wp_scroll_by(p, line, 0);
        }
        if (printable) fire(p, target, "keypress", 0, 0, kc, kn, mods);
    }
    fire(p, target, "keyup", 0, 0, kc, kn, mods);
    flush_nav(p);
}

/* ------------------------------------------------------------ varios */
void wp_eval_js(struct wpage *p, const char *code, char *out, uint32_t max)
{
    out[0] = 0; if (!p->js) { if (p->doc) start_js(p); if (!p->js) return; }
    wjs_eval_str(p->js, code, out, max); flush_nav(p);
}
void wp_find_scroll(struct wpage *p, const char *text)
{
    if (!p->doc || !p->lay || !text || !*text) return;
    struct wnode *n = p->doc->root; int cnt = 0; int hit = 0;
    while (n) {
        if (n->type == WN_TEXT && n->tlen && wu_findi(n->name, text)) {
            int x, y, w, h; if (wl_node_rect(p->lay, n->parent ? n->parent : n, &x, &y, &w, &h) && y + h > p->sy + 4 + (cnt ? 0 : 0)) {
                if (y > p->sy + 4 || cnt) { p->sy = y > 60 ? y - 60 : 0; wp_scroll_by(p, 0, 0); hit = 1; break; }
            }
            cnt++;
        }
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    set_status(p, hit ? WPS_OK : WPS_ERR, hit ? "Texto encontrado" : "Texto no encontrado");
}

/* ------------------------------------------------------------ latido */
int wp_tick(struct wpage *p, uint64_t now)
{
    int next = 120;
    if (p->dead) return 1000;
    if (p->nav_at && now >= p->nav_at) { p->nav_at = 0; p->nav_pending = 1; }
    if (p->nav_pending) { flush_nav(p); return 1; }
    if (p->js && !p->in_script && p->doc) {
        int ran = 0; p->forced = 0; int t = wjs_pump(p->js, now, &ran);
        if (t >= 0 && t < next) next = t;
        if (p->nav_pending) { flush_nav(p); return 1; }
    }
    if (p->doc && p->styles_pending) { p->styles_pending = 0; relayout(p, 1); }
    if (p->doc && p->resized && p->js) { p->resized = 0; wjs_dispatch(p->js, 0, "resize", 0, 0, 0, 0, 0); }
    uint32_t gap = 33; if (p->layout_dur * 3 > gap) gap = p->layout_dur * 3;
    if (p->doc && p->need_style && now - p->last_layout_ms >= gap) {
        uint64_t t0 = now_ms(); relayout(p, 0); p->layout_dur = (uint32_t)(now_ms() - t0);
    } else if (p->need_style) next = 10;
    if (p->doc && p->img_pending > 0 && !p->stop) {
        int did = 0;
        for (int k = 0; k < 2 && p->img_pending > 0; k++) did |= img_load_one(p);
        if (did) {
            p->phase = WP_IMAGES; char m[64] = "Cargando imagenes ("; int k = 19; k += wu_itoa(p->img_pending - p->img_waiting > 0 ? p->img_pending - p->img_waiting : 0, m + k); m[k++] = ')'; m[k] = 0; set_status(p, WPS_BUSY, m);
            if (now - p->last_img_layout > 1500 + (uint64_t)p->layout_dur * 8 || p->img_pending == 0) { p->last_img_layout = now_ms(); p->img_dirty = 0; relayout(p, 0); }
            else p->img_dirty = 1;          /* las que ya tienen hueco se ven al repintar; el resto, en la proxima maquetacion */
            next = 1;
        }
        if ((p->img_pending == 0 || !did) && p->phase == WP_IMAGES) { p->phase = WP_IDLE; p->img_dirty = 0; relayout(p, 0);
            { char m[112] = "Listo - imagenes: "; uint32_t k = 18; k += (uint32_t)wu_itoa(p->img_ok, m + k); wu_copy(m + k, " ok, ", sizeof m - k); k = wu_strlen(m); k += (uint32_t)wu_itoa(p->img_fail, m + k); wu_copy(m + k, " fallan", sizeof m - k);
              if (p->img_fail && p->img_why[0]) { k = wu_strlen(m); wu_copy(m + k, " (", sizeof m - k); k += 2; wu_copy(m + k, p->img_why, sizeof m - k); k = wu_strlen(m); if (k + 2 < sizeof m) { m[k++] = ')'; m[k] = 0; } }
              set_status(p, p->img_fail ? WPS_BUSY : WPS_OK, m); } }
    }
    if (p->focus && is_text_ctl(p->focus) && now - p->caret_t >= 530) { p->caret_t = now; p->caret_on = !p->caret_on; if (p->lay) p->lay->caret_on = p->caret_on; }
    if (p->nav_at && (int)(p->nav_at - now) < next) next = (int)(p->nav_at - now) > 0 ? (int)(p->nav_at - now) : 1;
    return next;
}
