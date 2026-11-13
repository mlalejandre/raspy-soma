/* PARCHE 082: x86_64 todavia sin nucleos secundarios: todo se ejecuta en el nucleo 0. */
#include "smp.h"
void smp_init(void) { }
int  smp_cpus(void) { return 1; }
int  smp_enabled(void) { return 1; }
void smp_set_enabled(int on) { (void)on; }
void smp_parallel(smp_fn fn, void *arg) { fn(0, 1, arg); }
int  smp_parallel_n(smp_fn fn, void *arg, int n) { (void)n; fn(0, 1, arg); return 1; }
int  smp_core_info(int i, struct smp_core_info *o) { if (i != 0) return -1; o->online = 1; o->mpidr = 0; o->jobs = 0; o->busy_ticks = 0; return 0; }
const char *smp_method(void) { return "un solo nucleo (x86_64)"; }
