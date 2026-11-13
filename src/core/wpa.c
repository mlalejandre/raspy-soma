/* PARCHE 080: suplicante WPA2-PSK y resultados de escaneo. Ver wpa.h. */
#include "wpa.h"
#include "crypto.h"

static const uint8_t OUI_RSN[3] = { 0x00, 0x0F, 0xAC }, OUI_MS[3] = { 0x00, 0x50, 0xF2 };
static int oui_is(const uint8_t *p, const uint8_t *o) { return p[0] == o[0] && p[1] == o[1] && p[2] == o[2]; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void cpy(uint8_t *d, const uint8_t *s, size_t n) { for (size_t i = 0; i < n; i++) d[i] = s[i]; }
static void zero(uint8_t *d, size_t n) { for (size_t i = 0; i < n; i++) d[i] = 0; }
static int cmp(const uint8_t *a, const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1; return 0; }

const char *wpa_sec_name(int sec)
{
    static const char *n[] = { "abierta", "WEP", "WPA", "WPA2", "WPA3", "WPA2/WPA3", "WPA2-Enterprise", "?" };
    return (sec >= 0 && sec <= SEC_UNKNOWN) ? n[sec] : "?";
}

int wpa_parse_rsn(const uint8_t *b, size_t len, struct rsn_info *o)
{
    zero((uint8_t *)o, sizeof *o);
    if (len < 2 || le16(b) != 1) return -1;
    size_t i = 2;
    o->present = 1; o->group = SUITE_CCMP; o->pairwise_ccmp = 1; o->akm_8021x = 1;     /* valores por defecto de la norma */
    if (i + 4 > len) return 0;
    if (oui_is(b + i, OUI_RSN)) o->group = b[i + 3];
    i += 4;
    if (i + 2 > len) return 0;
    uint16_t n = le16(b + i); i += 2;
    if (i + 4u * n > len) return -1;
    o->pairwise_ccmp = o->pairwise_tkip = 0;
    for (uint16_t k = 0; k < n; k++, i += 4) if (oui_is(b + i, OUI_RSN)) { if (b[i + 3] == SUITE_CCMP) o->pairwise_ccmp = 1; if (b[i + 3] == SUITE_TKIP) o->pairwise_tkip = 1; }
    if (i + 2 > len) return 0;
    n = le16(b + i); i += 2;
    if (i + 4u * n > len) return -1;
    o->akm_8021x = 0;
    for (uint16_t k = 0; k < n; k++, i += 4) if (oui_is(b + i, OUI_RSN)) {
        uint8_t t = b[i + 3];
        if (t == AKM_PSK) o->akm_psk = 1; else if (t == 6) o->akm_psk_sha256 = 1; else if (t == AKM_SAE) o->akm_sae = 1; else if (t == AKM_8021X || t == 5) o->akm_8021x = 1;
    }
    if (i + 2 <= len) o->caps = le16(b + i);
    return 0;
}

int wpa_classify(const uint8_t *ies, size_t len, int privacy, struct rsn_info *rsn)
{
    struct rsn_info tmp; if (!rsn) rsn = &tmp;
    zero((uint8_t *)rsn, sizeof *rsn);
    int wpa1 = 0;
    for (size_t i = 0; i + 2 <= len; ) {
        uint8_t id = ies[i], l = ies[i + 1];
        if (i + 2 + l > len) break;
        const uint8_t *b = ies + i + 2;
        if (id == 48) { struct rsn_info r; if (wpa_parse_rsn(b, l, &r) == 0) *rsn = r; }
        else if (id == 221 && l >= 4 && oui_is(b, OUI_MS) && b[3] == 1) wpa1 = 1;
        i += 2u + l;
    }
    if (rsn->present) {
        if (rsn->akm_sae && (rsn->akm_psk || rsn->akm_psk_sha256)) return SEC_WPA2_WPA3;
        if (rsn->akm_sae) return SEC_WPA3_SAE;
        if (rsn->akm_psk || rsn->akm_psk_sha256) return SEC_WPA2_PSK;
        if (rsn->akm_8021x) return SEC_ENTERPRISE;
        return SEC_UNKNOWN;
    }
    if (wpa1) return SEC_WPA;
    return privacy ? SEC_WEP : SEC_OPEN;
}

size_t wpa_build_rsn_ie(uint8_t group, uint8_t *o)
{
    const uint8_t t[22] = { 48, 20, 1, 0, 0x00, 0x0F, 0xAC, group, 1, 0, 0x00, 0x0F, 0xAC, SUITE_CCMP, 1, 0, 0x00, 0x0F, 0xAC, AKM_PSK, 0, 0 };
    cpy(o, t, 22);
    return 22;
}

/* ---------------------------------------------------------------- escaneo */
void wifi_chanspec(uint16_t cs, uint8_t *ch, uint8_t *b5)
{
    *ch = (uint8_t)(cs & 0xFF);
    *b5 = (cs & 0xC000u) == 0xC000u || *ch > 14;
}

/* Dos disposiciones posibles de bss_info en memoria (con alineacion natural, la de los compiladores, o empaquetada). Se usa la primera
 * cuyos campos de IE cuadren con la longitud. */
struct bss_layout { uint16_t chanspec, rssi, ie_off, ie_len, min; };
static const struct bss_layout LAYOUTS[2] = { { 72, 78, 116, 120, 126 }, { 71, 76, 109, 111, 117 } };

int wifi_escan_parse(const uint8_t *buf, size_t len, struct wifi_bss *list, int *count, int max)
{
    if (len < 12) return -1;
    uint32_t total = le32(buf);
    uint16_t nb = le16(buf + 10);
    if (total > len) total = (uint32_t)len;
    size_t off = 12;
    for (uint16_t k = 0; k < nb; k++) {
        if (off + 8 > total) return -1;
        const uint8_t *b = buf + off;
        uint32_t blen = le32(b + 4);
        if (blen < 117 || off + blen > total) return -1;
        const struct bss_layout *L = 0;
        for (int j = 0; j < 2 && !L; j++) {
            if (blen < LAYOUTS[j].min) continue;
            uint32_t io = le16(b + LAYOUTS[j].ie_off), il = le32(b + LAYOUTS[j].ie_len);
            if (io >= LAYOUTS[j].min && io <= blen && il <= blen - io) L = &LAYOUTS[j];
        }
        if (!L) return -1;
        struct wifi_bss e; zero((uint8_t *)&e, sizeof e);
        cpy(e.bssid, b + 8, 6);
        e.ssid_len = b[18] > 32 ? 32 : b[18];
        for (int i = 0; i < e.ssid_len; i++) { char c = (char)b[19 + i]; e.ssid[i] = (c >= 32 && c < 127) ? c : (c ? '?' : ' '); }
        e.ssid[e.ssid_len] = 0;
        wifi_chanspec(le16(b + L->chanspec), &e.channel, &e.band5);
        e.rssi = (int16_t)le16(b + L->rssi);
        struct rsn_info r;
        e.sec = wpa_classify(b + le16(b + L->ie_off), le32(b + L->ie_len), (le16(b + 16) & 0x10) != 0, &r);
        e.group = r.group;
        off += blen;
        int found = -1;
        for (int i = 0; i < *count; i++) if (!cmp(list[i].bssid, e.bssid, 6)) { found = i; break; }
        if (found >= 0) { if (e.rssi > list[found].rssi) list[found] = e; }
        else if (*count < max) list[(*count)++] = e;
    }
    return *count;
}

void wifi_bss_sort(struct wifi_bss *l, int n)
{
    for (int i = 1; i < n; i++) { struct wifi_bss t = l[i]; int j = i - 1; while (j >= 0 && l[j].rssi < t.rssi) { l[j + 1] = l[j]; j--; } l[j + 1] = t; }
}

/* ---------------------------------------------------------------- saludo de 4 pasos */
void wpa_derive_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6], const uint8_t an[32], const uint8_t sn[32], uint8_t ptk[48])
{
    uint8_t d[76];
    int m = cmp(aa, spa, 6) < 0;
    cpy(d, m ? aa : spa, 6); cpy(d + 6, m ? spa : aa, 6);
    int n = cmp(an, sn, 32) < 0;
    cpy(d + 12, n ? an : sn, 32); cpy(d + 44, n ? sn : an, 32);
    ieee80211_prf(pmk, 32, "Pairwise key expansion", d, 76, ptk, 48);
}
void wpa_mic(const uint8_t kck[16], const uint8_t *f, size_t len, uint8_t mic[16])
{
    uint8_t t[20]; hmac_sha1(kck, 16, f, len, t); cpy(mic, t, 16);
}

void wpa_sm_init(struct wpa_sm *s, const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6], const uint8_t sn[32], const uint8_t *ie, size_t ie_len)
{
    zero((uint8_t *)s, sizeof *s);
    cpy(s->pmk, pmk, 32); cpy(s->aa, aa, 6); cpy(s->spa, spa, 6); cpy(s->snonce, sn, 32);
    if (ie_len > sizeof s->ie) ie_len = sizeof s->ie;
    cpy(s->ie, ie, ie_len); s->ie_len = (uint8_t)ie_len;
}

const char *wpa_err_name(int r)
{
    switch (r) {
    case WPA_R_IGNORE: return "ignorada"; case WPA_R_SEND: return "responder"; case WPA_R_DONE: return "claves listas";
    case WPA_E_FORMAT: return "trama mal formada"; case WPA_E_MIC: return "MIC incorrecto (contrasena equivocada)";
    case WPA_E_REPLAY: return "contador repetido"; case WPA_E_KEYDATA: return "datos de clave ilegibles"; case WPA_E_STATE: return "fuera de orden";
    case WPA_E_NONCE: return "ANonce distinto del mensaje 1"; default: return "?";
    }
}

/* Respuesta EAPOL-Key: info, contador, nonce (o ceros), datos; MIC con la KCK */
static size_t build(const struct wpa_sm *s, uint8_t ver, uint16_t ki, const uint8_t *nonce, const uint8_t *data, size_t dlen, uint8_t *o)
{
    size_t body = 95 + dlen;
    zero(o, 4 + body);
    o[0] = ver; o[1] = 3; o[2] = (uint8_t)(body >> 8); o[3] = (uint8_t)body;
    o[4] = 2; o[5] = (uint8_t)(ki >> 8); o[6] = (uint8_t)ki;
    cpy(o + 9, s->replay, 8);
    if (nonce) cpy(o + 17, nonce, 32);
    o[97] = (uint8_t)(dlen >> 8); o[98] = (uint8_t)dlen;
    if (dlen) cpy(o + 99, data, dlen);
    uint8_t mic[16]; wpa_mic(s->ptk, o, 4 + body, mic); cpy(o + 81, mic, 16);
    return 4 + body;
}

static int check_mic(const struct wpa_sm *s, const uint8_t *f, size_t flen)
{
    static uint8_t t[2048];
    if (flen > sizeof t) return 0;
    cpy(t, f, flen); zero(t + 81, 16);
    uint8_t mic[16]; wpa_mic(s->ptk, t, flen, mic);
    uint8_t d = 0; for (int i = 0; i < 16; i++) d |= (uint8_t)(mic[i] ^ f[81 + i]);
    return d == 0;
}

/* Datos de clave cifrados: busca la KDE de la clave de grupo */
static int take_gtk(struct wpa_sm *s, const uint8_t *f, size_t dlen)
{
    static uint8_t kd[512];
    if (dlen < 24 || dlen % 8 || dlen - 8 > sizeof kd) return WPA_E_KEYDATA;
    if (aes_key_unwrap(s->ptk + 16, f + 99, dlen - 8, kd)) return WPA_E_KEYDATA;
    size_t n = dlen - 8;
    for (size_t i = 0; i + 2 <= n; ) {
        uint8_t id = kd[i], l = kd[i + 1];
        if (id == 0xDD && l == 0) break;                              /* relleno */
        if (i + 2 + l > n) return WPA_E_KEYDATA;
        if (id == 0xDD && l >= 6 && oui_is(kd + i + 2, OUI_RSN) && kd[i + 5] == 1) {
            size_t gl = l - 6u;
            if (gl < 5 || gl > 32) return WPA_E_KEYDATA;
            s->gtk_idx = kd[i + 6] & 3; s->gtk_len = (uint8_t)gl;
            cpy(s->gtk, kd + i + 8, gl);
            return 0;
        }
        i += 2u + l;
    }
    return WPA_E_KEYDATA;
}

int wpa_sm_rx(struct wpa_sm *s, const uint8_t *f, size_t len, uint8_t *out, size_t *out_len)
{
    if (len < EAPOL_KEY_HDR || f[1] != 3) return WPA_R_IGNORE;          /* no es EAPOL-Key */
    size_t flen = 4u + be16(f + 2);
    if (flen > len || flen < EAPOL_KEY_HDR || f[4] != 2) return WPA_E_FORMAT;
    uint16_t ki = be16(f + 5), dlen = be16(f + 97);
    if ((ki & KI_VER_MASK) != 2 || 99u + dlen > flen) return WPA_E_FORMAT;
    if (s->have_replay && cmp(f + 9, s->replay, 8) <= 0) return WPA_E_REPLAY;
    uint8_t ver = f[0];

    if ((ki & KI_PAIRWISE) && (ki & KI_ACK) && !(ki & KI_MIC)) {        /* mensaje 1 (puede repetirse) */
        if (s->state == WPA_DONE) return WPA_E_STATE;
        cpy(s->anonce, f + 17, 32);
        cpy(s->replay, f + 9, 8); s->have_replay = 1;
        wpa_derive_ptk(s->pmk, s->aa, s->spa, s->anonce, s->snonce, s->ptk);
        *out_len = build(s, ver, 2 | KI_PAIRWISE | KI_MIC, s->snonce, s->ie, s->ie_len, out);
        s->state = WPA_SENT2;
        return WPA_R_SEND;
    }
    if ((ki & KI_PAIRWISE) && (ki & KI_ACK) && (ki & KI_MIC) && (ki & KI_INSTALL)) {   /* mensaje 3 */
        if (s->state != WPA_SENT2) return WPA_E_STATE;
        if (cmp(f + 17, s->anonce, 32)) return WPA_E_NONCE;
        if (!check_mic(s, f, flen)) return WPA_E_MIC;
        if (!(ki & KI_ENCDATA)) return WPA_E_KEYDATA;
        int e = take_gtk(s, f, dlen); if (e) return e;
        cpy(s->replay, f + 9, 8);
        *out_len = build(s, ver, 2 | KI_PAIRWISE | KI_MIC | KI_SECURE, 0, 0, 0, out);
        s->state = WPA_DONE;
        return WPA_R_DONE;
    }
    if (!(ki & KI_PAIRWISE) && (ki & KI_ACK) && (ki & KI_MIC)) {         /* renovacion de la clave de grupo */
        if (s->state != WPA_DONE) return WPA_E_STATE;
        if (!check_mic(s, f, flen)) return WPA_E_MIC;
        int e = take_gtk(s, f, dlen); if (e) return e;
        cpy(s->replay, f + 9, 8);
        *out_len = build(s, ver, 2 | KI_MIC | KI_SECURE, 0, 0, 0, out);
        return WPA_R_DONE;
    }
    return WPA_R_IGNORE;
}
