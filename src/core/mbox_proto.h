#ifndef SOMA_MBOX_PROTO_H
#define SOMA_MBOX_PROTO_H
#include <stdint.h>
/* PARCHE 063: mensajes de la interfaz de propiedades del firmware de la Raspberry Pi (canal 8 del mailbox), parte pura: se construyen
 * y se interpretan en un buffer de palabras de 32 bits (probado en el host). Solo lo necesario para el framebuffer. */
#define MBOX_FB_WORDS 36
struct mbox_fb_result { uint32_t base, size, pitch, width, height, depth; };
/* Peticion: pantalla fisica y virtual w x h, profundidad 32, orden de pixeles (0 = BGR, 1 = RGB), desplazamiento 0, reservar buffer y
 * leer la pitch. Devuelve el numero de palabras escritas (MBOX_FB_WORDS) o 0 si cap es menor o w/h no son validos (1..8192). */
uint32_t mbox_fb_build(uint32_t *buf, uint32_t cap, uint32_t w, uint32_t h, uint32_t rgb);
/* Respuesta: 0 = bien. -1 mensaje no atendido, -2 etiqueta mal formada, -3 faltan etiquetas, -4 buffer no reservado o pitch incoherente. */
int mbox_fb_parse(const uint32_t *buf, uint32_t words, struct mbox_fb_result *r);
/* PARCHE 075 (Pi 5 real): el firmware de la Pi 5 solo atiende parte de las etiquetas y NO deja elegir la resolucion. Interpretacion
 * tolerante: se saltan las etiquetas no atendidas; vale con base, ancho y alto (de SET o GET). Si faltan, depth = 32 y pitch = ancho*depth/8.
 * 0 = bien; -1 mensaje no atendido; -2 mal formado; -4 sin buffer o sin tamano. */
int mbox_fb_parse_any(const uint32_t *buf, uint32_t words, struct mbox_fb_result *r);
/* PARCHE 078: el firmware de la Pi 5 real devuelve como aceptados el ancho, el alto y la profundidad PEDIDOS aunque no los aplique: la pantalla sigue en el
 * modo del monitor a 16 bits. Lo unico fiable es la pitch (bytes por fila) y el tamano reservado.
 *  mbox_fb_consistent: 1 si ancho/alto/profundidad cuadran con pitch y tamano (pitch = ancho*bytes y tamano/pitch = alto, con margen de redondeo).
 *  mbox_fb_fix: si no cuadran, recalcula: alto = tamano/pitch; ancho = pitch/bytes, con bytes por pixel = el que cuadre con el tamano fisico leido
 *  (phys_w, 0 si no se sabe) o, si no, 2 (RGB565) salvo que de una proporcion absurda, en cuyo caso 4. Devuelve 1 si ha corregido algo. */
int mbox_fb_consistent(const struct mbox_fb_result *r);
int mbox_fb_fix(struct mbox_fb_result *r, uint32_t phys_w, uint32_t phys_h);
/* Solo leer el tamano fisico (no reserva nada) e interpretar la respuesta: 0 = bien. */
uint32_t mbox_fb_build_getphys(uint32_t *buf, uint32_t cap);
int mbox_fb_parse_phys(const uint32_t *buf, uint32_t words, uint32_t *w, uint32_t *h);
/* Peticion minima: lee tamano fisico y profundidad actuales, reserva el buffer y lee la pitch (no cambia nada). */
uint32_t mbox_fb_build_query(uint32_t *buf, uint32_t cap);
#endif
