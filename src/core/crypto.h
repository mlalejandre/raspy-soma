#ifndef SOMA_CRYPTO_H
#define SOMA_CRYPTO_H
/* PARCHE 080: criptografia basica para WPA2-PSK, sin dependencias: SHA-1, HMAC-SHA1, PBKDF2-HMAC-SHA1 (clave de la red a partir de la
 * contrasena y el SSID), PRF de IEEE 802.11 (claves de sesion), AES-128 (cifrar y descifrar un bloque) y AES Key Wrap (RFC 3394, la
 * clave de grupo viaja envuelta). Probada en el host con los vectores oficiales (tests/test_wpa.c). No es de tiempo constante: vale para
 * un cliente Wi-Fi domestico, no para un servidor expuesto. */
#include <stdint.h>
#include <stddef.h>

struct sha1_ctx { uint32_t h[5]; uint64_t len; uint8_t buf[64]; uint32_t n; };
void sha1_init(struct sha1_ctx *c);
void sha1_update(struct sha1_ctx *c, const void *p, size_t n);
void sha1_final(struct sha1_ctx *c, uint8_t out[20]);
void sha1(const void *p, size_t n, uint8_t out[20]);

/* HMAC-SHA1 sobre varias piezas (n trozos) */
void hmac_sha1_v(const uint8_t *key, size_t klen, int n, const uint8_t *const *parts, const size_t *lens, uint8_t out[20]);
void hmac_sha1(const uint8_t *key, size_t klen, const void *msg, size_t mlen, uint8_t out[20]);

/* PBKDF2-HMAC-SHA1. WPA2: pbkdf2_sha1(pass, ssid, 4096 iteraciones, 32 bytes) = PMK. */
/* PARCHE 082: si se fija, PBKDF2 calcula sus bloques de 20 bytes en paralelo (la PMK de WPA2 son 2 bloques: casi la mitad de tiempo con 2 nucleos).
 * run(job, arg, n) debe ejecutar job(i, n, arg) para i = 0..n-1 y esperar; devuelve los trozos usados. */
extern int (*crypto_parallel)(void (*job)(int idx, int n, void *arg), void *arg, int n);
void pbkdf2_sha1(const char *pass, size_t plen, const uint8_t *salt, size_t slen, uint32_t iter, uint8_t *out, size_t olen);
int  wpa_psk_to_pmk(const char *pass, const uint8_t *ssid, size_t ssid_len, uint8_t pmk[32]);   /* 0 = bien; contrasena 8..63 o 64 hex */

/* PRF-n de 802.11: HMAC-SHA1(K, etiqueta || 0 || datos || i) concatenado hasta olen bytes. */
void ieee80211_prf(const uint8_t *key, size_t klen, const char *label, const uint8_t *data, size_t dlen, uint8_t *out, size_t olen);

struct aes128 { uint8_t rk[176]; };
void aes128_init(struct aes128 *a, const uint8_t key[16]);
void aes128_encrypt(const struct aes128 *a, const uint8_t in[16], uint8_t out[16]);
void aes128_decrypt(const struct aes128 *a, const uint8_t in[16], uint8_t out[16]);
/* RFC 3394 con KEK de 128 bits. n = bytes de datos claros (multiplo de 8, >= 16). unwrap: in de n+8 bytes; 0 = integridad correcta. */
int  aes_key_wrap(const uint8_t kek[16], const uint8_t *in, size_t n, uint8_t *out);
int  aes_key_unwrap(const uint8_t kek[16], const uint8_t *in, size_t n, uint8_t *out);
#endif
