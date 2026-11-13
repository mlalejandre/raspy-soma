/* PARCHE 080: pruebas de criptografia, WPA2-PSK, escaneo y capa netif. Vectores oficiales (FIPS 180, FIPS 197, RFC 2202, RFC 3394,
 * IEEE 802.11i anexo H) y un saludo de 4 pasos completo generado por una referencia independiente (tests/wpa_vectors.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/crypto.h"
#include "../src/core/wpa.h"
#include "../src/net/netif.h"
#include "wpa_vectors.h"

static int seq_par(void (*job)(int, int, void *), void *arg, int n) { for (int i = n - 1; i >= 0; i--) job(i, n, arg); return n; }
static void hex(const char *h, uint8_t *o) { for (size_t i = 0; h[2 * i]; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); o[i] = (uint8_t)v; } }

/* netif: dos dispositivos falsos */
static int a_ok, b_ok, a_sent, b_sent;
static const uint8_t MA[6] = { 1, 2, 3, 4, 5, 6 }, MB[6] = { 9, 9, 9, 9, 9, 9 };
static int a_init(void) { return a_ok; }
static int b_init(void) { return b_ok; }
static int a_send(const void *f, uint16_t n) { (void)f; a_sent += n; return 0; }
static int b_send(const void *f, uint16_t n) { (void)f; b_sent += n; return 0; }
static int a_poll(void *o, uint16_t m, uint16_t *l) { (void)o; (void)m; *l = 0; return 0; }
static const uint8_t *a_mac(void) { return MA; }
static const uint8_t *b_mac(void) { return MB; }
static const struct netif_ops IA = { "eth0", "a", a_init, a_send, a_poll, a_mac, 0, 0 }, IB = { "wlan0", "b", b_init, b_send, a_poll, b_mac, 0, 0 };

int main(void)
{
    uint8_t d[64], e[64];
    /* SHA-1 */
    sha1("abc", 3, d); hex("a9993e364706816aba3e25717850c26c9cd0d89d", e); assert(!memcmp(d, e, 20));
    sha1("", 0, d); hex("da39a3ee5e6b4b0d3255bfef95601890afd80709", e); assert(!memcmp(d, e, 20));
    {   static char m[1000000]; memset(m, 'a', sizeof m); struct sha1_ctx c; sha1_init(&c);
        for (int i = 0; i < 10; i++) sha1_update(&c, m + i * 100000, 100000);
        sha1_final(&c, d); hex("34aa973cd4c4daa4f61eeb2bdbad27316534016f", e); assert(!memcmp(d, e, 20)); }
    /* HMAC-SHA1 (RFC 2202 casos 1 y 6) */
    { uint8_t k[80]; memset(k, 0x0b, 20); hmac_sha1(k, 20, "Hi There", 8, d); hex("b617318655057264e28bc0b6fb378c8ef146be00", e); assert(!memcmp(d, e, 20));
      memset(k, 0xaa, 80); hmac_sha1(k, 80, "Test Using Larger Than Block-Size Key - Hash Key First", 54, d); hex("aa4ae5e15272d00e95705637ce8a3b55ed402112", e); assert(!memcmp(d, e, 20)); }
    /* PBKDF2 / PMK (IEEE 802.11i H.4) */
    { uint8_t pmk[32]; assert(wpa_psk_to_pmk("password", (const uint8_t *)"IEEE", 4, pmk) == 0); assert(!memcmp(pmk, PBK_IEEE, 32));
      assert(wpa_psk_to_pmk("ThisIsAPassword", (const uint8_t *)"ThisIsASSID", 11, pmk) == 0);
      hex("0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af", e); assert(!memcmp(pmk, e, 32));
      assert(wpa_psk_to_pmk("corta", (const uint8_t *)"x", 1, pmk) == -1);
      assert(wpa_psk_to_pmk("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", (const uint8_t *)"x", 1, pmk) == 0 && pmk[0] == 0x01 && pmk[31] == 0xef); }
    /* PARCHE 082: la ruta en paralelo da lo mismo */
    { uint8_t a1[32], a2[40], b1[32], b2[40];
      pbkdf2_sha1("password", 8, (const uint8_t *)"IEEE", 4, 4096, a1, 32); pbkdf2_sha1("x1234567", 8, (const uint8_t *)"s", 1, 7, a2, 40);
      crypto_parallel = seq_par;
      pbkdf2_sha1("password", 8, (const uint8_t *)"IEEE", 4, 4096, b1, 32); pbkdf2_sha1("x1234567", 8, (const uint8_t *)"s", 1, 7, b2, 40);
      crypto_parallel = 0;
      assert(!memcmp(a1, b1, 32) && !memcmp(a1, PBK_IEEE, 32) && !memcmp(a2, b2, 40)); }
    /* PRF (IEEE 802.11i H.3) */
    { uint8_t k[20]; memset(k, 0x0b, 20); uint8_t o[64]; ieee80211_prf(k, 20, "prefix", (const uint8_t *)"Hi There", 8, o, 64); assert(!memcmp(o, PRF_REF, 64)); }
    /* AES-128 (FIPS 197 C.1) */
    { uint8_t k[16], p[16], c[16], b[16]; for (int i = 0; i < 16; i++) k[i] = (uint8_t)i; hex("00112233445566778899aabbccddeeff", p);
      struct aes128 a; aes128_init(&a, k); aes128_encrypt(&a, p, c); hex("69c4e0d86a7b0430d8cdb78070b4c55a", e); assert(!memcmp(c, e, 16)); assert(!memcmp(c, AESCT, 16));
      aes128_decrypt(&a, c, b); assert(!memcmp(b, p, 16)); }
    /* AES Key Wrap (RFC 3394 4.1) */
    { uint8_t kek[16], kd[16], w[24], u[16]; hex("000102030405060708090a0b0c0d0e0f", kek); hex("00112233445566778899aabbccddeeff", kd);
      assert(aes_key_wrap(kek, kd, 16, w) == 0); hex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", e); assert(!memcmp(w, e, 24));
      assert(aes_key_unwrap(kek, w, 16, u) == 0 && !memcmp(u, kd, 16)); w[3] ^= 1; assert(aes_key_unwrap(kek, w, 16, u) == -2); }

    /* Saludo de 4 pasos completo frente a la referencia */
    {
        struct wpa_sm s; uint8_t ie[32], out[512]; size_t ol = 0;
        size_t il = wpa_build_rsn_ie(SUITE_CCMP, ie);
        uint8_t aa[6], spa[6], sn[32]; hex("020000000001", aa); hex("dca632aabbcc", spa); for (int i = 0; i < 32; i++) sn[i] = (uint8_t)(100 + i);
        wpa_sm_init(&s, PMK, aa, spa, sn, ie, il);
        assert(wpa_sm_rx(&s, M3, sizeof M3, out, &ol) == WPA_E_STATE);              /* mensaje 3 antes del 1 */
        assert(wpa_sm_rx(&s, M1, sizeof M1, out, &ol) == WPA_R_SEND);
        assert(!memcmp(s.ptk, PTK, 48));
        assert(ol == sizeof M2 && !memcmp(out, M2, ol));                               /* mensaje 2 identico, MIC incluido */
        { uint8_t bad[sizeof M3]; memcpy(bad, M3, sizeof M3); bad[120] ^= 1;          /* datos alterados: MIC no cuadra */
          assert(wpa_sm_rx(&s, bad, sizeof bad, out, &ol) == WPA_E_MIC); }
        assert(wpa_sm_rx(&s, M3, sizeof M3, out, &ol) == WPA_R_DONE);
        assert(ol == sizeof M4 && !memcmp(out, M4, ol));
        assert(s.gtk_len == 16 && s.gtk_idx == 1 && !memcmp(s.gtk, GTK, 16) && s.state == WPA_DONE);
        assert(wpa_sm_rx(&s, M3, sizeof M3, out, &ol) == WPA_E_REPLAY);             /* repetido */
        assert(wpa_sm_rx(&s, G1, sizeof G1, out, &ol) == WPA_R_DONE);               /* renovacion de grupo */
        assert(ol == sizeof G2 && !memcmp(out, G2, ol) && s.gtk_idx == 2 && !memcmp(s.gtk, GTK2, 16));
        /* contrasena equivocada: el mensaje 3 falla por MIC */
        struct wpa_sm w; uint8_t pmk2[32]; wpa_psk_to_pmk("otra-contrasena", (const uint8_t *)"SOMA-casa", 9, pmk2);
        wpa_sm_init(&w, pmk2, aa, spa, sn, ie, il);
        assert(wpa_sm_rx(&w, M1, sizeof M1, out, &ol) == WPA_R_SEND && memcmp(out, M2, ol));
        assert(wpa_sm_rx(&w, M3, sizeof M3, out, &ol) == WPA_E_MIC);
        /* basura */
        for (size_t n = 0; n < sizeof M3; n++) { struct wpa_sm z = s; (void)wpa_sm_rx(&z, M3, n, out, &ol); }
        assert(!strcmp(wpa_err_name(WPA_E_MIC), "MIC incorrecto (contrasena equivocada)"));
    }
    /* Escaneo */
    {
        struct wifi_bss l[8]; int n = 0;
        assert(wifi_escan_parse(ESC, sizeof ESC, l, &n, 8) == 3);
        wifi_bss_sort(l, n);
        assert(!strcmp(l[0].ssid, "SOMA-casa") && l[0].rssi == -48 && l[0].channel == 6 && !l[0].band5 && l[0].sec == SEC_WPA2_PSK && l[0].group == SUITE_CCMP);
        assert(!strcmp(l[1].ssid, "Libre") && l[1].sec == SEC_OPEN && l[1].channel == 11);
        assert(!strcmp(l[2].ssid, "Vecino5G") && l[2].band5 && l[2].channel == 36 && l[2].sec == SEC_WPA2_WPA3);
        int m = 0; assert(wifi_escan_parse(ESC, 40, l, &m, 8) < 0);
        for (size_t k = 0; k < sizeof ESC; k++) { int c = 0; (void)wifi_escan_parse(ESC, k, l, &c, 8); }
        /* WEP y WPA1 */
        assert(wpa_classify((const uint8_t *)"", 0, 1, 0) == SEC_WEP);
        { const uint8_t w1[] = { 221, 6, 0x00, 0x50, 0xF2, 1, 1, 0 }; assert(wpa_classify(w1, sizeof w1, 1, 0) == SEC_WPA); }
        { const uint8_t ent[] = { 48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 1, 0, 0 }; assert(wpa_classify(ent, sizeof ent, 1, 0) == SEC_ENTERPRISE); }
        assert(!strcmp(wpa_sec_name(SEC_WPA3_SAE), "WPA3"));
    }
    /* netif */
    {
        assert(netif_send("x", 1) == -1 && netif_mac()[0] == 0);
        netif_register(&IA); netif_register(&IB); netif_register(&IA);
        assert(netif_count() == 2);
        a_ok = 0; b_ok = 1;
        assert(netif_start_first() == 1 && netif_active() == &IB);                 /* eth0 falla: wlan0 */
        assert(netif_send("hola", 4) == 0 && b_sent == 4 && netif_mac() == MB);
        assert(netif_use("eth0") == 0 && netif_active() == &IB);                   /* sigue fallando: no cambia */
        a_ok = 1;
        assert(netif_use("eth0") == 1 && netif_active() == &IA && netif_send("ab", 2) == 0 && a_sent == 2);
        assert(netif_use("nada") == 0 && netif_is_up(0) && netif_is_up(1));
    }
    puts("test_wpa: OK");
    return 0;
}
