#ifndef SOMA_ACCT_H
#define SOMA_ACCT_H
/* PARCHE 089: contabilidad para el Monitor del sistema. Cada hilo tiene un "dueno" (byte) que se usa para:
 *  - etiquetar las reservas del heap (mem.c guarda el dueno en cada bloque) -> memoria por aplicacion o subsistema;
 *  - repartir el tiempo de CPU -> CPU por aplicacion aunque todas las ventanas compartan el hilo de la GUI.
 * La GUI cambia el dueno al dar paso a cada aplicacion (acct_set_owner) y lo devuelve despues. */
#include <stdint.h>
#define ACCT_SYSTEM  0
#define ACCT_RAMFS   1
#define ACCT_SCREEN  2
#define ACCT_NET     3
#define ACCT_AGENT   4
#define ACCT_DESKTOP 5
#define ACCT_WEB     6           /* descargas del navegador (su hilo de trabajo) */
#define ACCT_APP0    16          /* 16 + enum app_mode */
#define ACCT_IDLE    255
uint8_t  acct_set_owner(uint8_t o);          /* devuelve el anterior */
uint64_t acct_owner_cycles(uint8_t o);       /* ciclos acumulados (contador de arch_cycle_counter) */
void     thread_set_owner(void *tcb, uint8_t o);
struct thread_info { uint32_t tid, state; char name[32]; uint64_t cycles, switches; uint32_t stack_kb; uint8_t owner, user; };
int      thread_snapshot(struct thread_info *out, int max);
uint64_t acct_now(void);                      /* ciclos */
#endif
