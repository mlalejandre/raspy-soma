#ifndef SOMA_WPA_H
#define SOMA_WPA_H
/* PARCHE 080: cliente WPA2-PSK (lado "suplicante") y lectura de resultados de escaneo del firmware Broadcom. Logica pura, sin E/S:
 *  - elementos de informacion (IE) de una red: SSID, RSN (WPA2/WPA3), WPA (vendedor) -> tipo de seguridad;
 *  - resultados de escaneo "escan" del firmware (bss_info) -> lista de redes con nombre, BSSID, senal, canal y seguridad;
 *  - saludo de 4 pasos EAPOL-Key (mensajes 1-4) con CCMP: PTK por la PRF, MIC HMAC-SHA1, clave de grupo envuelta con AES.
 * El firmware del chip se encarga del cifrado de las tramas; SOMA solo negocia las claves y se las instala (fase 3). */
#include <stdint.h>
#include <stddef.h>

/* ---------------- seguridad de una red ---------------- */
enum { SEC_OPEN = 0, SEC_WEP, SEC_WPA, SEC_WPA2_PSK, SEC_WPA3_SAE, SEC_WPA2_WPA3, SEC_ENTERPRISE, SEC_UNKNOWN };
const char *wpa_sec_name(int sec);
#define SUITE_TKIP 2
#define SUITE_CCMP 4
#define AKM_8021X 1
#define AKM_PSK   2
#define AKM_SAE   8
struct rsn_info { int present; uint8_t group, pairwise_ccmp, pairwise_tkip, akm_psk, akm_sae, akm_8021x, akm_psk_sha256; uint16_t caps; };
/* Interpreta el cuerpo de un IE RSN (sin id ni longitud). 0 = bien. */
int wpa_parse_rsn(const uint8_t *b, size_t len, struct rsn_info *o);
/* Recorre los IE: copia el SSID (si hay), rellena rsn y dice si hay IE WPA (version 1). Devuelve el tipo de seguridad. privacy = bit de la
 * capacidad del AP (cifrado sin RSN/WPA = WEP). */
int wpa_classify(const uint8_t *ies, size_t len, int privacy, struct rsn_info *rsn);
/* IE RSN que anuncia SOMA: WPA2-PSK, CCMP par, grupo = group (el del AP). Devuelve los bytes (22). */
size_t wpa_build_rsn_ie(uint8_t group, uint8_t *out);

/* ---------------- resultados de escaneo ---------------- */
struct wifi_bss { char ssid[33]; uint8_t ssid_len, bssid[6], channel, band5; int16_t rssi; int sec; uint8_t group; };
/* Interpreta un resultado "escan" (cabecera de 12 bytes + bss_info). Anade las redes a list (se fusionan por BSSID y se queda la de mas
 * senal); devuelve cuantas hay en list o <0 si el bloque esta mal formado. */
int wifi_escan_parse(const uint8_t *buf, size_t len, struct wifi_bss *list, int *count, int max);
/* Ordena por senal descendente */
void wifi_bss_sort(struct wifi_bss *list, int n);
/* Canal a partir de un chanspec del firmware (codificacion 802.11ac): canal y si es 5 GHz. */
void wifi_chanspec(uint16_t cs, uint8_t *channel, uint8_t *band5);

/* ---------------- saludo de 4 pasos ---------------- */
enum { WPA_IDLE = 0, WPA_SENT2, WPA_DONE };
enum { WPA_R_IGNORE = 0, WPA_R_SEND = 1, WPA_R_DONE = 2, WPA_E_FORMAT = -1, WPA_E_MIC = -2, WPA_E_REPLAY = -3, WPA_E_KEYDATA = -4, WPA_E_STATE = -5, WPA_E_NONCE = -6 };
struct wpa_sm {
    uint8_t pmk[32], aa[6], spa[6], anonce[32], snonce[32];
    uint8_t ptk[48];                       /* KCK(16) | KEK(16) | TK(16) */
    uint8_t gtk[32]; uint8_t gtk_len, gtk_idx;
    uint8_t replay[8]; int have_replay;
    uint8_t ie[64]; uint8_t ie_len;        /* nuestro IE RSN (va en el mensaje 2) */
    int state;
};
/* snonce: 32 bytes aleatorios que aporta quien llama (el kernel los saca de su generador). */
void wpa_sm_init(struct wpa_sm *s, const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6], const uint8_t snonce[32], const uint8_t *ie, size_t ie_len);
/* Procesa una trama EAPOL (desde la cabecera 802.1X, sin Ethernet). Si devuelve WPA_R_SEND o WPA_R_DONE, out (*out_len bytes) es la
 * respuesta EAPOL que hay que enviar al AP. Con WPA_R_DONE las claves estan listas: TK = ptk+32, GTK = gtk (indice gtk_idx). */
int wpa_sm_rx(struct wpa_sm *s, const uint8_t *eapol, size_t len, uint8_t *out, size_t *out_len);
const char *wpa_err_name(int r);
/* Utilidades para quien hace de AP en las pruebas */
#define EAPOL_KEY_HDR 99                      /* 4 de 802.1X + 95 del descriptor de clave */
#define KI_VER_MASK  0x0007
#define KI_PAIRWISE  0x0008
#define KI_INSTALL   0x0040
#define KI_ACK       0x0080
#define KI_MIC       0x0100
#define KI_SECURE    0x0200
#define KI_ENCDATA   0x1000
void wpa_derive_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6], const uint8_t an[32], const uint8_t sn[32], uint8_t ptk[48]);
void wpa_mic(const uint8_t kck[16], const uint8_t *frame, size_t len, uint8_t mic[16]);   /* frame con el MIC a cero */
#endif
