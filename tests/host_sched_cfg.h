/* Configuracion de pruebas del planificador (se inyecta con -include en test_sched).
 * Las pilas de hilo viven en una arena estatica y hay pocas ranuras para poder agotarlas. */
#ifndef HOST_SCHED_CFG_H
#define HOST_SCHED_CFG_H

#include <stdint.h>

#define HOST_STACK_SLOTS  48
#define HOST_STACK_BYTES  (HOST_STACK_SLOTS * 0x10000ULL)

extern uint8_t host_stack_arena[HOST_STACK_BYTES];

#define THREAD_STACK_BASE ((uint64_t)(uintptr_t)host_stack_arena)
#define THREAD_MAX_SLOTS  HOST_STACK_SLOTS
#define SOMA_HOST_REAL_THREAD 1
#define SOMA_HOST_PANIC 1                /* panic.c con limites de .text simulados */

#endif
