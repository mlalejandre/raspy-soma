#include "mutex.h"
#include "console.h"
#include "thread.h"
#include "panic.h"
#include "idt.h"

#define MAX_REGISTERED_MUTEXES 32
static struct kmutex *mutex_registry[MAX_REGISTERED_MUTEXES];
static int mutex_registry_count = 0;

/* SOMA_PARCHE_009_MUTEX: quien espera que mutex (para detectar interbloqueos). */
#define MAX_WAITERS 16
struct kmutex_wait { struct tcb *t; struct kmutex *m; };
static struct kmutex_wait waits[MAX_WAITERS];
static uint32_t deadlocks_found, long_waits, misuse_reports;
void (*kmutex_wait_hook)(struct kmutex *m, uint32_t iter) = 0;

uint32_t kmutex_deadlocks_found(void) { return deadlocks_found; }
uint32_t kmutex_long_waits(void) { return long_waits; }
uint32_t kmutex_misuse_reports(void) { return misuse_reports; }

void kmutex_wait_register(struct tcb *t, struct kmutex *m)
{
    int free_slot = -1;
    if (!t) return;
    for (int i = 0; i < MAX_WAITERS; ++i) {
        if (waits[i].t == t) { waits[i].m = m; return; }
        if (!waits[i].t && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) { waits[free_slot].t = t; waits[free_slot].m = m; }
}

void kmutex_wait_unregister(struct tcb *t)
{
    if (!t) return;
    for (int i = 0; i < MAX_WAITERS; ++i) {
        if (waits[i].t == t) { waits[i].t = 0; waits[i].m = 0; }
    }
}

int kmutex_waiters(const struct kmutex *m)
{
    int n = 0;
    for (int i = 0; i < MAX_WAITERS; ++i) if (waits[i].t && waits[i].m == m) n++;
    return n;
}

static struct kmutex *wait_of(struct tcb *t)
{
    for (int i = 0; i < MAX_WAITERS; ++i) {
        if (waits[i].t == t) return waits[i].m;
    }
    return 0;
}

static const char *state_name(uint32_t s)
{
    switch (s) {
    case THREAD_STATE_READY:    return "READY";
    case THREAD_STATE_RUNNING:  return "RUNNING";
    case THREAD_STATE_SLEEPING: return "SLEEPING";
    case THREAD_STATE_DEAD:     return "DEAD";
    default:                    return "?";
    }
}

static const char *tname(const struct tcb *t) { return t ? t->name : "(ninguno)"; }

/* 1 si el hilo `waiter`, al esperar `m`, cerraria un ciclo: m -> owner -> lo que este espera -> ... */
int kmutex_would_deadlock(struct tcb *waiter, struct kmutex *m)
{
    struct tcb *o = m ? m->owner : 0;
    for (int hops = 0; o && hops <= MAX_WAITERS; ++hops) {
        struct kmutex *next;
        if (o == waiter) return 1;
        next = wait_of(o);
        if (!next || !next->locked) return 0;
        o = next->owner;
    }
    return 0;
}

/* noinline: su direccion es el #0 del backtrace y la cadena de frames sigue desde quien espera. */
static void __attribute__((noinline)) report_deadlock(struct tcb *waiter, struct kmutex *m)
{
    struct tcb *o = m->owner;
    deadlocks_found++;
    kprint("\n[MUTEX] INTERBLOQUEO detectado: '"); kprint(tname(waiter));
    kprint("' espera '"); kprint(m->name); kprint("' (de '"); kprint(tname(o)); kprint("')");
    for (int hops = 0; o && o != waiter && hops <= MAX_WAITERS; ++hops) {
        struct kmutex *next = wait_of(o);
        if (!next) break;
        kprint(" -> '"); kprint(tname(o)); kprint("' espera '"); kprint(next->name);
        kprint("' (de '"); kprint(tname(next->owner)); kprint("')");
        o = next->owner;
    }
    kprint("\n");
#ifndef SOMA_HOST_PANIC
    panic_print_context((uint64_t)(uintptr_t)&report_deadlock, 0,
                        (uint64_t)(uintptr_t)__builtin_frame_address(0), 0);
#endif
}

static void report_long_wait(struct tcb *waiter, struct kmutex *m, uint64_t waited_ms)
{
    long_waits++;
    kprint("\n[MUTEX] '"); kprint(tname(waiter)); kprint("' espera "); kprint_dec((uint32_t)(waited_ms / 1000u));
    kprint(" s el mutex '"); kprint(m->name); kprint("' retenido por '"); kprint(tname(m->owner));
    kprint("' ("); kprint(m->owner ? state_name(m->owner->state) : "?"); kprint(")\n");
}

void kmutex_init(struct kmutex *m, const char *name)
{
    if (!m) return;
    m->locked = 0;
    m->owner = 0;
    m->depth = 0;
    m->name = name ? name : "mutex";

    /* Auto-registrar mutex en la tabla global si no existe */
    for (int i = 0; i < mutex_registry_count; ++i) {
        if (mutex_registry[i] == m) return;
    }
    if (mutex_registry_count < MAX_REGISTERED_MUTEXES) {
        mutex_registry[mutex_registry_count++] = m;
    }
}

/* SOMA_PARCHE_009_MUTEX: antes, quien esperaba un mutex ocupado hacia thread_yield() en
 * bucle. Si el propietario estaba dormido (p. ej. kernel_main reteniendo net_lock mientras
 * espera la red), los demas hilos listos se pasaban la CPU sin parar: 1,5 millones de
 * cambios de contexto por segundo y el hilo idle sin correr. Ahora: unas pocas cesiones
 * (por si el propietario esta a punto de soltarlo) y despues dormir 1 ms; si el propietario
 * esta dormido, se duerme desde el primer intento.
 * timeout_ms == 0: esperar sin limite. Devuelve 1 si se adquirio, 0 si vencio el timeout. */
static int lock_impl(struct kmutex *m, uint32_t timeout_ms)
{
    struct tcb *curr = thread_current();
    uint64_t start = 0;
    uint32_t iter = 0;
    int registered = 0, warned = 0, dl_reported = 0;

    while (1) {
        if (!m->locked) {
            m->locked = 1;
            m->owner = curr;
            m->depth = 1;
            if (registered) kmutex_wait_unregister(curr);
            return 1;
        }
        if (m->owner == curr) {
            m->depth++;     /* Adquisicion recursiva por el mismo hilo */
            if (registered) kmutex_wait_unregister(curr);
            return 1;
        }

        if (!registered) {
            registered = 1;
            start = timer_get_uptime_ms();
            kmutex_wait_register(curr, m);
        }
        {
            uint64_t waited = timer_get_uptime_ms() - start;
            if (!dl_reported && curr && kmutex_would_deadlock(curr, m)) {
                dl_reported = 1;
                report_deadlock(curr, m);
            }
            if (!warned && waited >= KMUTEX_WARN_MS) {
                warned = 1;
                report_long_wait(curr, m, waited);
            }
            if (timeout_ms && waited >= timeout_ms) {
                kmutex_wait_unregister(curr);
                return 0;
            }
        }

        if (iter >= KMUTEX_SPIN_YIELDS || (m->owner && m->owner->state == THREAD_STATE_SLEEPING)) {
            thread_sleep(1);
        } else {
            thread_yield();
        }
        if (kmutex_wait_hook) kmutex_wait_hook(m, iter);
        iter++;
    }
}

void kmutex_lock(struct kmutex *m)
{
    if (!m) return;
    (void)lock_impl(m, 0);
}

int kmutex_lock_timeout(struct kmutex *m, uint32_t timeout_ms)
{
    if (!m) return 0;
    return lock_impl(m, timeout_ms ? timeout_ms : 1);
}

int kmutex_trylock(struct kmutex *m)
{
    if (!m) return 0;
    struct tcb *curr = thread_current();

    if (!m->locked) {
        m->locked = 1;
        m->owner = curr;
        m->depth = 1;
        return 1;
    }
    if (m->owner == curr) {
        m->depth++;
        return 1;
    }
    return 0;
}

/* Uso incorrecto: se avisa (maximo 8 veces) pero se mantiene el comportamiento anterior. */
static void report_misuse(const char *what, struct kmutex *m)
{
    misuse_reports++;
    if (misuse_reports > 8) return;
    kprint("\n[MUTEX] "); kprint(what); kprint(": '"); kprint(m->name);
    kprint("' (hilo '"); kprint(tname(thread_current())); kprint("', propietario '");
    kprint(tname(m->owner)); kprint("')\n");
}

void kmutex_unlock(struct kmutex *m)
{
    if (!m) return;
    if (!m->locked) {
        report_misuse("unlock de un mutex que no estaba bloqueado", m);
    } else if (m->owner != thread_current()) {
        report_misuse("unlock por un hilo que no es el propietario", m);
    }
    if (m->depth > 1) {
        m->depth--;         /* unlock interno: seguimos siendo propietarios */
        return;
    }
    m->depth = 0;
    m->locked = 0;
    m->owner = 0;
}

void kmutex_release_all_for_thread(struct tcb *t)
{
    if (!t) return;
    kmutex_wait_unregister(t);      /* un hilo muerto ya no espera nada */
    for (int i = 0; i < mutex_registry_count; ++i) {
        struct kmutex *m = mutex_registry[i];
        if (m && m->locked && m->owner == t) {
            kprint("\n[MUTEX RECOVERY] Mutex '");
            kprint(m->name);
            kprint("' liberado forzosamente tras muerte de TID ");
            kprint_dec(t->tid);
            kprint("\n");

            m->depth = 0;
            m->locked = 0;
            m->owner = 0;
        }
    }
}

static void pad_to(const char *s, uint32_t width)
{
    uint32_t n = 0;
    kprint(s);
    while (s[n]) n++;
    for (; n < width; ++n) kputc(' ');
}

void kmutex_dump(void)
{
    kprint("\nMUTEX DEL KERNEL:\n------------------------------------------------------------\n");
    kprint(" Nombre            Estado   Propietario       Prof. Esperan\n");
    kprint("------------------------------------------------------------\n");
    for (int i = 0; i < mutex_registry_count; ++i) {
        struct kmutex *m = mutex_registry[i];
        if (!m) continue;
        kputc(' ');
        pad_to(m->name, 17);
        pad_to(m->locked ? "ocupado" : "libre", 9);
        pad_to(m->locked ? tname(m->owner) : "-", 18);
        kprint_dec((uint32_t)m->depth);
        kprint("     ");
        kprint_dec((uint32_t)kmutex_waiters(m));
        kprint("\n");
    }
    kprint("------------------------------------------------------------\n");
    kprint("Interbloqueos detectados: "); kprint_dec(deadlocks_found);
    kprint(" | Esperas largas (>"); kprint_dec(KMUTEX_WARN_MS / 1000u);
    kprint(" s): "); kprint_dec(long_waits);
    kprint(" | Usos incorrectos: "); kprint_dec(misuse_reports); kprint("\n\n");
}

/* Auto-test de sincronizacion */
static struct kmutex test_mtx;
static volatile int shared_counter = 0;
static volatile int mtx_worker1_done = 0;
static volatile int mtx_worker2_done = 0;

static void mtx_worker_func(void *arg)
{
    int id = (int)(uintptr_t)arg;
    for (int i = 0; i < 50; ++i) {
        kmutex_lock(&test_mtx);
        int val = shared_counter;
        thread_yield(); /* Forzar conmutacion en seccion critica */
        shared_counter = val + 1;
        kmutex_unlock(&test_mtx);
    }
    if (id == 1) mtx_worker1_done = 1;
    if (id == 2) mtx_worker2_done = 1;
}

int kmutex_test_self(void)
{
    kprint("\n[KMUTEX AUTO-TEST] Verificando exclusion mutua con dos hilos en seccion critica...\n");

    kmutex_init(&test_mtx, "test_counter_lock");
    shared_counter = 0;
    mtx_worker1_done = 0;
    mtx_worker2_done = 0;

    thread_create("mtx_w1", mtx_worker_func, (void *)1);
    thread_create("mtx_w2", mtx_worker_func, (void *)2);

    while (!mtx_worker1_done || !mtx_worker2_done) {
        thread_yield();
    }

    /* Limpiar los dos hilos muertos con el reaper */
    thread_reap_dead();

    if (shared_counter != 100) {
        kprint("  FALLO: Condicion de carrera detectada (contador=");
        kprint_dec(shared_counter);
        kprint(", esperado 100)\n");
        return 0;
    }

    kprint("  Seccion critica protegida OK (100 incrementos concurrentes sin conflicto)\n");

    /* Prueba 2: Recuperacion forzada de mutex huerfano tras thread_kill */
    kprint("  Probando recuperacion de mutex huerfano tras kill...\n");
    kmutex_lock(&test_mtx);

    /* Simular que el dueño era un hilo efimero con TID ficticio 99 */
    struct tcb fake_dead_thread;
    fake_dead_thread.tid = 99;
    test_mtx.owner = &fake_dead_thread;

    /* Invocar recuperacion forzada */
    kmutex_release_all_for_thread(&fake_dead_thread);

    if (test_mtx.locked != 0 || test_mtx.owner != 0) {
        kprint("  FALLO: Mutex no fue limpiado tras muerte del hilo\n");
        return 0;
    }

    /* Debe poder re-adquirirse limpiamente sin deadlock */
    kmutex_lock(&test_mtx);
    kmutex_unlock(&test_mtx);
    kprint("  Recuperacion de bloqueo huerfano verificada OK (sin deadlocks)\n");

    kprint("[KMUTEX AUTO-TEST] SUPERADO CON EXITO (Concurrencia y Kill Seguro OK).\n\n");
    return 1;
}
