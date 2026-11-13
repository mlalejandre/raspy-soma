#ifndef MYOS_CONSOLE_H
#define MYOS_CONSOLE_H

#include <stdint.h>

#define CMD_LINE_MAX 4096

void kputc(char c);
void kprint(const char *s);
void kprint_hex8(uint8_t value);
void kprint_hex16(uint16_t value);
void kprint_hex32(uint32_t value);
void kprint_dec(uint32_t value);
void kprint_mac(const uint8_t *mac);
void kprint_ip(const uint8_t *ip);

int  kgetc_ready(void);
char kgetc(void);
int  kgetline(char *buf, uint32_t max);
void console_clear(void);
extern volatile int console_muted;
/* PARCHE 116: captura de la salida de UN hilo (el que llama a begin) en un buffer, con entrada simulada para kgetline: sirve para ejecutar programas de prueba
 * sin ensuciar la pantalla ni mezclar la salida de otros hilos. `input` = lineas separadas por \n. end() devuelve los bytes capturados. abort() (panico) la corta. */
void console_capture_begin(char *buf, uint32_t cap, const char *input);
uint32_t console_capture_end(void);
void console_capture_abort(void);
void keyboard_irq_handler(void);
void keyboard_inject_scancode(uint8_t sc);   /* PARCHE 047: scancode PS/2 (juego 1) desde el teclado USB (solo x86_64) */
void keyboard_reset_state(void);

int  console_history_dump(char *out_buf, uint32_t max_out);
void console_history_load_from_vfs(void);
void console_history_sync_to_vfs(void);

#endif
