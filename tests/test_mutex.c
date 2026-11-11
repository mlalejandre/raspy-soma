/* Pruebas en host de src/core/mutex.c sobre el planificador real (thread.c).
 * Se compila con -include tests/host_sched_cfg.h. El "hook" de espera emula el paso del
 * tiempo y a los demas hilos: tras cada paso del hilo que espera avanza el reloj 1 ms y
 * devuelve la CPU al hilo que espera, como haria el planificador real. */
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "thread.h"
#include "mutex.h"

enum { MAXRUN = 64, MAXIT = 4096 };
static uint64_t T;
static void tick(uint64_t ms) { T += ms; host_time_set(T); }
static uint32_t cur(void) { return thread_current()->tid; }
static void worker(void *a) { (void)a; }

static int run_until(uint32_t tid)
{
    for (int i = 0; i < MAXRUN; ++i) {
        if (cur() == tid) return 1;
        thread_yield();
    }
    return cur() == tid;
}

static struct tcb *A, *B, *C;

/* --- estado compartido de los hooks --- */
static struct tcb *waiter;
static uint32_t release_at, step_ms, iters;
static int waiters_seen;
static uint32_t state_at[MAXIT];

static void hook(struct kmutex *m, uint32_t iter)
{
    if (iter < MAXIT) state_at[iter] = waiter->state;    /* estado tras el paso de espera */
    if (iter == 0) waiters_seen = kmutex_waiters(m);     /* debe figurar como esperando */
    iters = iter + 1;
    tick(step_ms);
    run_until(waiter->tid);                              /* el planificador le devuelve la CPU */
    if (iter + 1 == release_at) { m->locked = 0; m->owner = 0; m->depth = 0; }  /* el propietario suelta */
}

static void arm(struct tcb *w, uint32_t release, uint32_t ms)
{
    waiter = w; release_at = release; step_ms = ms; iters = 0;
    memset(state_at, 0xFF, sizeof(state_at));
    kmutex_wait_hook = hook;
}

static void disarm(void) { kmutex_wait_hook = 0; }

static void test_sin_contencion(void)
{
    static struct kmutex m;

    kmutex_init(&m, "libre");
    kmutex_lock(&m);
    CHECK_EQ_INT(m.locked, 1); CHECK(m.owner == thread_current()); CHECK_EQ_INT(m.depth, 1);
    kmutex_lock(&m);                                     /* recursivo */
    CHECK_EQ_INT(m.depth, 2);
    kmutex_unlock(&m);
    CHECK_EQ_INT(m.locked, 1); CHECK_EQ_INT(m.depth, 1);
    kmutex_unlock(&m);
    CHECK_EQ_INT(m.locked, 0); CHECK(m.owner == 0);
    CHECK_EQ_INT(kmutex_lock_timeout(&m, 10), 1);
    kmutex_unlock(&m);
    CHECK_EQ_INT(kmutex_misuse_reports(), 0);
}

/* Propietario listo: primero unas cesiones, despues dormir 1 ms (no girar). */
static void test_retroceso_con_propietario_listo(void)
{
    static struct kmutex m;
    uint32_t i;

    kmutex_init(&m, "m_listo");
    CHECK(run_until(A->tid));
    kmutex_lock(&m);                                     /* A es el propietario y sigue READY */
    CHECK(run_until(B->tid));
    arm(B, 9, 1);
    kmutex_lock(&m);                                     /* B espera 9 pasos y entonces lo obtiene */
    disarm();
    CHECK(m.owner == B); CHECK_EQ_INT(m.locked, 1);
    CHECK_EQ_INT(waiters_seen, 1);
    CHECK_EQ_INT(kmutex_waiters(&m), 0);                 /* y deja de figurar al adquirirlo */
    CHECK_EQ_INT(iters, 9);
    for (i = 0; i < KMUTEX_SPIN_YIELDS; ++i) CHECK_EQ_INT(state_at[i], THREAD_STATE_READY);     /* cede */
    for (i = KMUTEX_SPIN_YIELDS; i < 9; ++i) CHECK_EQ_INT(state_at[i], THREAD_STATE_SLEEPING);  /* duerme */
    kmutex_unlock(&m);
}

/* Propietario dormido: girar no sirve de nada; se duerme desde el primer intento. */
static void test_propietario_dormido_no_se_gira(void)
{
    static struct kmutex m;

    kmutex_init(&m, "m_dormido");
    CHECK(run_until(A->tid));
    kmutex_lock(&m);
    thread_sleep(100000);                                /* A duerme reteniendo el mutex */
    CHECK_EQ_INT(A->state, THREAD_STATE_SLEEPING);
    CHECK(run_until(B->tid));
    arm(B, 3, 1);
    kmutex_lock(&m);
    disarm();
    CHECK_EQ_INT(state_at[0], THREAD_STATE_SLEEPING);    /* sin fase de cesion */
    CHECK_EQ_INT(state_at[1], THREAD_STATE_SLEEPING);
    CHECK(m.owner == B);
    kmutex_unlock(&m);
    tick(200000);                                        /* despierta A: deja el estado limpio */
    CHECK(run_until(A->tid));
}

static void test_timeout(void)
{
    static struct kmutex m;
    int r;

    kmutex_init(&m, "m_timeout");
    CHECK(run_until(A->tid));
    kmutex_lock(&m);
    CHECK(run_until(B->tid));
    arm(B, 0, 1);                                        /* nadie lo suelta */
    r = kmutex_lock_timeout(&m, 50);
    disarm();
    CHECK_EQ_INT(r, 0);
    CHECK(m.owner == A);                                 /* sigue siendo de A */
    CHECK(iters >= 50 && iters <= 53);                   /* ~50 ms a 1 ms por paso */
    CHECK_EQ_INT(kmutex_waiters(&m), 0);                 /* B ya no figura esperando */

    arm(B, 7, 1);                                        /* se suelta a mitad de la espera */
    r = kmutex_lock_timeout(&m, 50);
    disarm();
    CHECK_EQ_INT(r, 1);
    CHECK(m.owner == B);
    kmutex_unlock(&m);
}

static void test_aviso_de_espera_larga(void)
{
    static struct kmutex m;
    uint32_t antes = kmutex_long_waits();
    const char *p, *q;

    kmutex_init(&m, "m_largo");
    CHECK(run_until(A->tid));
    kmutex_lock(&m);
    CHECK(run_until(B->tid));
    host_out_reset();
    arm(B, 8, 2500);                                     /* 2,5 s por paso: ~20 s de espera */
    kmutex_lock(&m);
    disarm();
    p = strstr(host_out(), "espera 10 s el mutex 'm_largo' retenido por 'A'");
    CHECK(p != 0);
    q = p ? strstr(p + 1, "espera 10 s el mutex 'm_largo'") : 0;
    CHECK(q == 0);                                       /* un solo aviso por espera */
    CHECK_EQ_INT(kmutex_long_waits(), antes + 1);
    kmutex_unlock(&m);
}

static void test_interbloqueo_de_dos(void)
{
    static struct kmutex m1, m2;
    uint32_t antes = kmutex_deadlocks_found();

    kmutex_init(&m1, "M1"); kmutex_init(&m2, "M2");
    CHECK(run_until(A->tid)); kmutex_lock(&m1);
    CHECK(run_until(B->tid)); kmutex_lock(&m2);
    kmutex_wait_register(A, &m2);                        /* A espera M2 (de B) */
    CHECK_EQ_INT(kmutex_would_deadlock(B, &m1), 1);      /* B esperando M1 (de A) cierra el ciclo */
    host_out_reset();
    arm(B, 6, 1);
    kmutex_lock(&m1);                                    /* B espera M1: interbloqueo real */
    disarm();
    CHECK(strstr(host_out(), "INTERBLOQUEO") != 0);
    CHECK(strstr(host_out(), "'B' espera 'M1' (de 'A')") != 0);
    CHECK(strstr(host_out(), "'A' espera 'M2' (de 'B')") != 0);
    CHECK_EQ_INT(kmutex_deadlocks_found(), antes + 1);   /* se avisa una sola vez */
    kmutex_wait_unregister(A);
    kmutex_unlock(&m1); kmutex_unlock(&m2);
}

static void test_interbloqueo_de_tres(void)
{
    static struct kmutex m1, m2, m3;
    uint32_t antes = kmutex_deadlocks_found();

    kmutex_init(&m1, "X1"); kmutex_init(&m2, "X2"); kmutex_init(&m3, "X3");
    CHECK(run_until(A->tid)); kmutex_lock(&m1);
    CHECK(run_until(B->tid)); kmutex_lock(&m2);
    CHECK(run_until(C->tid)); kmutex_lock(&m3);
    kmutex_wait_register(A, &m2);                        /* A -> X2 (de B) */
    kmutex_wait_register(B, &m3);                        /* B -> X3 (de C) */
    host_out_reset();
    arm(C, 5, 1);
    kmutex_lock(&m1);                                    /* C -> X1 (de A): ciclo A->B->C->A */
    disarm();
    CHECK(strstr(host_out(), "INTERBLOQUEO") != 0);
    CHECK(strstr(host_out(), "'C' espera 'X1' (de 'A')") != 0);
    CHECK(strstr(host_out(), "'A' espera 'X2' (de 'B')") != 0);
    CHECK(strstr(host_out(), "'B' espera 'X3' (de 'C')") != 0);
    CHECK_EQ_INT(kmutex_deadlocks_found(), antes + 1);
    kmutex_wait_unregister(A); kmutex_wait_unregister(B);
    kmutex_unlock(&m1); kmutex_unlock(&m2); kmutex_unlock(&m3);
}

/* Cadenas de espera que NO son ciclos no deben avisar. */
static void test_sin_falsos_positivos(void)
{
    static struct kmutex m1, m3, libre;

    kmutex_init(&m1, "F1"); kmutex_init(&m3, "F3"); kmutex_init(&libre, "F_libre");
    CHECK(run_until(A->tid)); kmutex_lock(&m1);
    CHECK(run_until(C->tid)); kmutex_lock(&m3);          /* C retiene F3 y no espera nada */
    kmutex_wait_register(A, &m3);                        /* A espera F3 (de C) */
    CHECK_EQ_INT(kmutex_would_deadlock(B, &m1), 0);      /* B -> F1 -> A -> F3 -> C -> (nada) */
    CHECK_EQ_INT(kmutex_would_deadlock(B, &libre), 0);   /* mutex libre: sin propietario */
    CHECK_EQ_INT(kmutex_would_deadlock(B, 0), 0);        /* puntero nulo tolerado */
    kmutex_wait_register(C, &libre);                     /* espera obsoleta sobre un mutex libre */
    CHECK_EQ_INT(kmutex_would_deadlock(B, &m1), 0);
    kmutex_wait_unregister(A); kmutex_wait_unregister(C);
    kmutex_unlock(&m1); kmutex_unlock(&m3);
}

static void test_uso_incorrecto(void)
{
    static struct kmutex m;
    uint32_t antes = kmutex_misuse_reports();
    int i;

    kmutex_init(&m, "m_mal");
    CHECK(run_until(A->tid));
    host_out_reset();
    kmutex_unlock(&m);                                   /* no estaba bloqueado */
    CHECK(strstr(host_out(), "unlock de un mutex que no estaba bloqueado: 'm_mal'") != 0);
    CHECK_EQ_INT(m.locked, 0);

    kmutex_lock(&m);                                     /* lo toma A... */
    CHECK(run_until(B->tid));
    host_out_reset();
    kmutex_unlock(&m);                                   /* ...y lo suelta B */
    CHECK(strstr(host_out(), "no es el propietario") != 0);
    CHECK_EQ_INT(m.locked, 0);                           /* el comportamiento se mantiene */

    host_out_reset();
    for (i = 0; i < 20; ++i) kmutex_unlock(&m);
    CHECK_EQ_INT(kmutex_misuse_reports(), antes + 22);   /* todos contados... */
    {
        int n = 0;
        const char *p = host_out();
        while ((p = strstr(p, "[MUTEX]")) != 0) { n++; p += 7; }
        CHECK(n <= 6);                                   /* ...pero solo se imprimen los primeros 8 en total */
    }
}

static void test_muerte_del_hilo_limpia_esperas(void)
{
    static struct kmutex m, other;

    kmutex_init(&m, "m_muerte"); kmutex_init(&other, "m_otro");
    CHECK(run_until(A->tid)); kmutex_lock(&m);
    kmutex_wait_register(B, &m);                         /* B espera un mutex de A */
    kmutex_wait_register(A, &other);                     /* y A espera otro mutex */
    CHECK_EQ_INT(kmutex_waiters(&m), 1);
    CHECK_EQ_INT(kmutex_waiters(&other), 1);
    kmutex_release_all_for_thread(A);                    /* A muere: suelta sus mutex y sus esperas */
    CHECK_EQ_INT(m.locked, 0);
    CHECK_EQ_INT(kmutex_waiters(&other), 0);             /* la espera de A desaparecio */
    CHECK_EQ_INT(kmutex_waiters(&m), 1);                 /* la de B no es suya: se conserva */
    kmutex_wait_unregister(B);
    CHECK_EQ_INT(kmutex_waiters(&m), 0);
}

static void test_volcado(void)
{
    host_out_reset();
    kmutex_dump();
    CHECK(strstr(host_out(), "MUTEX DEL KERNEL") != 0);
    CHECK(strstr(host_out(), "m_listo") != 0);
    CHECK(strstr(host_out(), "Interbloqueos detectados: 2") != 0);
    CHECK(strstr(host_out(), "Esperas largas (>10 s): 1") != 0);
}

int main(void)
{
    thread_init();
    A = thread_create("A", worker, 0);
    B = thread_create("B", worker, 0);
    C = thread_create("C", worker, 0);
    CHECK(A && B && C);

    test_sin_contencion();
    test_retroceso_con_propietario_listo();
    test_propietario_dormido_no_se_gira();
    test_timeout();
    test_aviso_de_espera_larga();
    test_interbloqueo_de_dos();
    test_interbloqueo_de_tres();
    test_sin_falsos_positivos();
    test_uso_incorrecto();
    test_muerte_del_hilo_limpia_esperas();
    test_volcado();
    TEST_MAIN_END("mutex");
}
