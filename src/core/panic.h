#ifndef SOMA_PANIC_H
#define SOMA_PANIC_H

#include <stdint.h>

/* SOMA_PARCHE_008_PANIC: diagnostico de fallos.
 *  - backtrace por cadena de frame pointers (x86_64 y AArch64 comparten el formato
 *    [fp] = fp anterior, [fp+8] = direccion de retorno) con validacion estricta
 *  - vigilante: avisa si ningun hilo cede la CPU (schedule()) durante mucho tiempo */

#define PANIC_MAX_FRAMES   16
#define WATCHDOG_LIMIT_MS  5000u

/* Lee 8 bytes de `addr`; devuelve 0 si no es legible (en el kernel: via VMM). */
typedef int (*panic_read64_fn)(uint64_t addr, uint64_t *out);

/* Recorre la cadena de frames empezando en `fp`. Guarda hasta `max` direcciones de
 * retorno en ret_out y devuelve cuantas. Se detiene ante un frame mal alineado, por
 * debajo de sp_floor (si != 0), ilegible, que no avance hacia direcciones mayores o
 * que salte demasiado (pila corrupta). Nunca lee fuera de lo que `rd` permita. */
int  panic_walk_frames(uint64_t fp, uint64_t sp_floor, panic_read64_fn rd,
                       uint64_t *ret_out, int max);

/* Imprime hilo actual + backtrace (pc, lr opcional, frames). */
void panic_print_context_with(panic_read64_fn rd, uint64_t pc, uint64_t lr,
                              uint64_t fp, uint64_t sp);
void panic_print_context(uint64_t pc, uint64_t lr, uint64_t fp, uint64_t sp);

/* ---- Vigilante ---- */
struct wd_state {
    uint64_t last_beat_seen;   /* ultimo latido observado */
    uint64_t last_report;      /* instante del ultimo aviso */
    uint32_t reports;          /* avisos desde el ultimo latido */
    uint32_t trips;            /* avisos totales */
};

/* Decision pura: 1 si hay que avisar ahora. Avisa al cumplirse limit_ms sin latidos y
 * repite cada limit_ms mientras siga colgado; un latido nuevo lo rearma. */
int  wd_evaluate(struct wd_state *st, uint64_t now_ms, uint64_t last_beat_ms, uint32_t limit_ms);

/* Imprime un valor de 64 bits como 0x + 16 digitos (kprint_hex32 solo llega a 32). */
void panic_hex64(uint64_t v);

void     watchdog_beat(uint64_t now_ms);   /* lo llama schedule() */
void     watchdog_tick(uint64_t now_ms, uint64_t pc, uint64_t fp, uint64_t sp); /* IRQ de reloj */
uint32_t watchdog_trips(void);
uint64_t watchdog_last_beat_ms(void);


/* PARCHE 109: guarda de panico anidado. El manejador de excepciones llama a panic_enter() al empezar el informe: devuelve 0 la primera vez
 * y el nivel de anidamiento (1, 2, ...) si una excepcion ocurre MIENTRAS se imprime el informe (p. ej. una pila corrupta o una consola rota).
 * Sin esta guarda el manejador se reentraba a si mismo hasta desbordar la pila: triple fallo y reinicio en bucle, sin ningun diagnostico. */
int  panic_enter(void);
void panic_reset(void);   /* solo para pruebas */

#endif
