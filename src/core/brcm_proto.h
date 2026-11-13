#ifndef SOMA_BRCM_PROTO_H
#define SOMA_BRCM_PROTO_H
/* PARCHE 081: protocolo de control del firmware FullMAC de Broadcom/Cypress por SDIO (parte pura, probada en tests/test_brcm.c):
 *  - SDPCM: cabecera de 12 bytes de cada trama de la funcion 2 (longitud y su complemento, secuencia, canal, credito);
 *  - BCDC: ordenes al firmware (cabecera de 16 bytes con numero de orden, longitud, id y estado) y cabecera de 4 bytes de datos/eventos;
 *  - iovars ("nombre\0" + valor), parametros de escaneo, clave para el chip, descarga del CLM y mascara de eventos;
 *  - eventos del firmware (trama Ethernet 0x886C con cabecera de Broadcom): tipo, estado, motivo, direccion y datos.
 * Formatos reescritos a partir del driver brcmfmac de Linux (sin copiar codigo). */
#include <stdint.h>
#include <stddef.h>

/* ---- SDPCM ---- */
#define SDPCM_HDR      12
#define SDPCM_CH_CTRL  0
#define SDPCM_CH_EVENT 1
#define SDPCM_CH_DATA  2
#define SDPCM_CH_GLOM  3
struct sdpcm { uint16_t len; uint8_t seq, chan, next, doff, fc, credit; };
/* Escribe la cabecera para una trama de len bytes en total (cabecera incluida). */
void sdpcm_build(uint8_t *o, uint16_t len, uint8_t seq, uint8_t chan);
/* 0 = bien; -1 longitud y complemento no cuadran; -2 desplazamiento de datos fuera de la trama. len 0 = no hay trama. */
int  sdpcm_parse(const uint8_t *b, size_t avail, struct sdpcm *o);
/* Hay credito para enviar la secuencia seq si (credit - seq) mod 256 esta en 1..0x40 */
int  sdpcm_can_send(uint8_t credit, uint8_t seq);

/* ---- BCDC ---- */
#define BCDC_HDR        16
#define BCDC_F_ERROR    0x01u
#define BCDC_F_SET      0x02u
#define BCDC_DATA_HDR   4
/* Orden: cabecera + datos (len bytes de payload). Devuelve bytes escritos. */
size_t bcdc_build(uint8_t *o, uint32_t cmd, int set, uint16_t id, const void *payload, uint32_t len);
struct bcdc { uint32_t cmd, len, flags, status; uint16_t id; };
int  bcdc_parse(const uint8_t *b, size_t len, struct bcdc *o);          /* 0 = bien */
/* Cabecera de datos: version 2; ifidx 0. Devuelve 4. Al recibir, bcdc_data_skip da los bytes a saltar (4 + desplazamiento*4) o -1. */
size_t bcdc_data_build(uint8_t *o);
int  bcdc_data_skip(const uint8_t *b, size_t len);

/* Ordenes del firmware (WLC_*) */
#define WLC_UP          2
#define WLC_DOWN        3
#define WLC_SET_INFRA   20
#define WLC_SET_AUTH    22
#define WLC_GET_BSSID   23
#define WLC_SET_SSID    26
#define WLC_DISASSOC    52
#define WLC_SET_KEY     45
#define WLC_SET_PM      86
#define WLC_GET_RSSI    127
#define WLC_SET_WSEC    134
#define WLC_SET_WPA_AUTH 165
#define WLC_GET_VAR     262
#define WLC_SET_VAR     263
/* iovar: "nombre\0" seguido de val (vlen bytes). Devuelve bytes o 0 si no cabe. */
size_t brcm_iovar(uint8_t *o, size_t cap, const char *name, const void *val, size_t vlen);

/* Escaneo "escan" (version 1, accion 1 = empezar). ssid opcional (NULL = todas). Devuelve bytes (72). */
#define ESCAN_PARAMS_LEN 72
size_t brcm_escan_params(uint8_t *o, uint16_t sync_id, const uint8_t *ssid, size_t ssid_len);
/* SSID para unirse: longitud (4) + 32 bytes */
size_t brcm_ssid(uint8_t *o, const uint8_t *ssid, size_t len);
/* Clave para el chip (WLC_SET_KEY). algo 4 = AES-CCM. pairwise: ea = direccion del AP; grupo: ea = 0. Devuelve bytes (164). */
#define WSEC_KEY_LEN 164
#define CRYPTO_ALGO_AES_CCM 4
size_t brcm_wsec_key(uint8_t *o, uint32_t index, const uint8_t *key, uint32_t klen, uint32_t algo, int primary, const uint8_t ea[6]);
/* Trozo de descarga del CLM ("clmload"): bandera inicio/fin, tipo 2 (CLM). Devuelve bytes (12 + n). */
size_t brcm_clm_chunk(uint8_t *o, const uint8_t *data, uint16_t n, int first, int last);
/* Mascara de eventos ("event_msgs", 16 bytes) con los eventos ev[0..n-1] activos */
void brcm_event_mask(uint8_t m[16], const uint32_t *ev, int n);

/* ---- eventos ---- */
#define BRCM_E_SET_SSID     0
#define BRCM_E_JOIN         1
#define BRCM_E_AUTH         3
#define BRCM_E_DEAUTH       5
#define BRCM_E_DEAUTH_IND   6
#define BRCM_E_ASSOC        7
#define BRCM_E_DISASSOC     11
#define BRCM_E_DISASSOC_IND 12
#define BRCM_E_LINK         16
#define BRCM_E_PSK_SUP      46
#define BRCM_E_ESCAN_RESULT 69
#define BRCM_ESCAN_PARTIAL  8
struct brcm_event { uint32_t type, status, reason, auth, datalen; uint16_t flags; uint8_t addr[6]; const uint8_t *data; };
/* b = trama Ethernet completa (destino, origen, tipo 0x886C, ...). 0 = evento valido. */
int  brcm_event_parse(const uint8_t *b, size_t len, struct brcm_event *o);
const char *brcm_event_name(uint32_t t);
#endif
