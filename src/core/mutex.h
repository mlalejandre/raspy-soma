#ifndef MYOS_MUTEX_H
#define MYOS_MUTEX_H

#include <stdint.h>
#include "thread.h"

struct kmutex {
    struct tcb   *owner;    /* Offset 0: 8 bytes */
    const char   *name;     /* Offset 8: 8 bytes */
    volatile int  locked;   /* Offset 16: 4 bytes */
    volatile int  depth;    /* Offset 20: 4 bytes -> total 24 B */
} __attribute__((aligned(8)));

/* kmutex_init registra el puntero en una tabla global (para kmutex_dump y la recuperacion
 * tras la muerte de un hilo): el mutex debe tener almacenamiento estatico, no de pila. */
void kmutex_init(struct kmutex *m, const char *name);
void kmutex_lock(struct kmutex *m);
int  kmutex_trylock(struct kmutex *m);
void kmutex_unlock(struct kmutex *m);
void kmutex_release_all_for_thread(struct tcb *t);
int  kmutex_test_self(void);

/* SOMA_PARCHE_009_MUTEX: espera con retroceso, deteccion de interbloqueos, timeouts. */
int  kmutex_lock_timeout(struct kmutex *m, uint32_t timeout_ms);   /* 1 = adquirido, 0 = vencio */
int  kmutex_would_deadlock(struct tcb *waiter, struct kmutex *m);  /* 1 si esperar m cierra un ciclo */
void kmutex_wait_register(struct tcb *t, struct kmutex *m);        /* usado por kmutex_lock; expuesto */
void kmutex_wait_unregister(struct tcb *t);                        /* para pruebas y diagnostico */
void kmutex_dump(void);                                            /* estado de todos los mutex */
int  kmutex_waiters(const struct kmutex *m);                       /* hilos esperando m ahora mismo */
uint32_t kmutex_deadlocks_found(void);
uint32_t kmutex_long_waits(void);
uint32_t kmutex_misuse_reports(void);
/* Solo pruebas: se invoca tras cada paso de espera de un kmutex_lock bloqueado. */
extern void (*kmutex_wait_hook)(struct kmutex *m, uint32_t iter);

#define KMUTEX_SPIN_YIELDS  4      /* cesiones antes de empezar a dormir 1 ms */
#define KMUTEX_WARN_MS      10000u /* aviso de espera larga (el arranque espera ~2 s a la red: es normal) */

#endif
