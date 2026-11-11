#ifndef SOMA_WEB_NET_H
#define SOMA_WEB_NET_H
#include "web_util.h"

#define WN_URL_MAX 1024
struct wn_url { char scheme[12]; char host[128]; int port; char path[WN_URL_MAX]; char frag[128]; int has_host; };

struct wn_req {
    const char *url; const char *method;                  /* method NULL = GET */
    const char *body; uint32_t body_len; const char *ctype;
    const char *referer; const char *accept;
    uint32_t timeout_ms; uint32_t max_body; int no_redirect; int want_binary;
    int via_gateway;                                      /* SOMA_PARCHE_028_UI: pedir tambien http:// a traves de la pasarela (conversion de imagenes) */
};
struct wn_resp {
    int status; char *body; uint32_t len;
    char ctype[96]; char charset[24]; char final_url[WN_URL_MAX]; int redirects; int via_gateway;
    char err[96];
};

int  wn_url_parse(const char *url, struct wn_url *u);                        /* 0 = OK */
int  wn_url_resolve(const char *base, const char *ref, char *out, uint32_t max);
void wn_url_origin(const char *url, char *out, uint32_t max);                /* esquema://host[:puerto] */
void wn_url_encode(const char *s, char *out, uint32_t max);
int  wn_fetch(const struct wn_req *rq, struct wn_resp *rs);                  /* 0 = respuesta recibida (mira rs->status) */
void wn_free(struct wn_resp *rs);

/* cookies */
void wn_cookie_store(const char *url, const char *setcookie);
int  wn_cookie_header(const char *url, char *out, uint32_t max, int for_js);   /* longitud */
void wn_cookie_clear(void);
/* ajustes */
void wn_set_gateway(const uint8_t ip[4]);
/* SOMA_PARCHE_028_UI: lo que sabe hacer la pasarela del anfitrion (cabecera X-Soma-Gateway). 0 = desconocida o antigua.
 * Las URL "soma-gw:/ruta" son ordenes directas a la pasarela (p. ej. POST soma-gw:/svg para rasterizar un <svg> en linea). */
#define WN_GW_PRESENT 1
#define WN_GW_IMAGES  2     /* convierte WebP/AVIF/ICO a PNG/JPEG */
#define WN_GW_SVG     4     /* rasteriza SVG */
int  wn_gateway_caps(void);
#endif
