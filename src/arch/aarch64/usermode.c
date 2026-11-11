/* SOMA_PARCHE_014_RING3: marcador de posicion; EL0 llegara con la Fase 5 (VBAR/GIC). */
#include "usermode.h"
#include "console.h"
#include "thread.h"

/* ganchos del planificador: sin modo usuario no hacen nada */
void arch_thread_switch(struct tcb *prev, struct tcb *next) { (void)prev; (void)next; }
void arch_thread_cleanup(struct tcb *t) { (void)t; }
int usermode_selftest(void)
{
    kprint("USERMODE: no disponible en AArch64 todavia\n");
    return -1;
}

int usermode_exec(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, int *err)
{ (void)name; (void)img; (void)len; (void)max_ms; (void)args; if (err) *err = 0; return -1; }
int64_t usermode_wait(int idx) { (void)idx; return -1; }
int usermode_format_table(char *out, uint32_t max, int reap) { (void)reap; if (max) out[0] = 0; return 0; }
void usermode_install_programs(void) { }
