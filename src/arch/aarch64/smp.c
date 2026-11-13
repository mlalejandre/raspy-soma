/* PARCHE 082: nucleos secundarios en aarch64 (Pi 5 real, Pi 5 emulada y QEMU virt). Ver src/core/smp.h.
 * Arranque por PSCI CPU_ON (SMC si el firmware nos dejo en EL2 o es la Pi 5; HVC en QEMU virt sin EL2). El nucleo destino se nombra por
 * su MPIDR: en la Pi 5 (Cortex-A76) el numero de nucleo va en Aff1 (bit MT activo); en QEMU virt en Aff0.
 * Cada secundario tiene una ranura de trabajo: el nucleo 0 la rellena, publica un numero de secuencia (store-release) y despierta con sev;
 * el secundario ejecuta y publica la misma secuencia como hecha. Sin interrupciones en los secundarios. */
#include <stdint.h>
#include "smp.h"
#include "platform.h"
#include "console.h"
#include "idt.h"

#define SMP_MAX 4
uint64_t soma_boot_el;                                         /* lo escribe boot.s */
struct { uint64_t mair, tcr, ttbr0, sctlr; } smp_mmu_cfg __attribute__((aligned(64)));
uint8_t smp_stacks[SMP_MAX][64 * 1024] __attribute__((aligned(64)));
void smp_secondary_entry(void);

struct slot {
    volatile smp_fn fn; void *volatile arg; volatile int idx, n;
    volatile uint32_t seq, done;
    volatile int online; uint32_t mpidr;
    volatile uint64_t jobs, busy;
} __attribute__((aligned(64)));
static struct slot g_slot[SMP_MAX];
static int g_ncpu = 1, g_inited, g_enabled = 1, g_busy;
static const char *g_method = "un solo nucleo";

static void dc_civac(const volatile void *p, uint64_t n)
{
    uint64_t a = (uint64_t)(uintptr_t)p & ~63ULL, e = (uint64_t)(uintptr_t)p + n;
    for (; a < e; a += 64) __asm__ volatile ("dc civac, %0" :: "r"(a) : "memory");
    __asm__ volatile ("dsb sy" ::: "memory");
}

static int64_t psci_cpu_on(int smc, uint64_t target, uint64_t entry, uint64_t ctx)
{
    register uint64_t x0 __asm__("x0") = 0xC4000003ULL, x1 __asm__("x1") = target, x2 __asm__("x2") = entry, x3 __asm__("x3") = ctx;
    if (smc) __asm__ volatile ("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
    else     __asm__ volatile ("hvc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
    return (int64_t)x0;
}

void smp_secondary_main(uint64_t idx)
{
    struct slot *s = &g_slot[idx];
    uint64_t m; __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(m));
    s->mpidr = (uint32_t)m;
    __atomic_store_n(&s->online, 1, __ATOMIC_RELEASE);
    __asm__ volatile ("sev");
    uint32_t last = 0;
    for (;;) {
        uint32_t q = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        if (q == last) { __asm__ volatile ("wfe"); continue; }
        uint64_t t0 = timer_ticks;
        s->fn(s->idx, s->n, s->arg);
        s->busy += timer_ticks - t0; s->jobs++;
        last = q;
        __atomic_store_n(&s->done, q, __ATOMIC_RELEASE);
        __asm__ volatile ("dsb ish\n sev" ::: "memory");
    }
}

void smp_init(void)
{
    if (g_inited) return;
    g_inited = 1;
    uint64_t m0; __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(m0));
    g_slot[0].online = 1; g_slot[0].mpidr = (uint32_t)m0;
#if defined(PLATFORM_rpi5)
    int smc = 1;
#else
    int smc = soma_boot_el >= 2;
#endif
    int aff1 = (m0 >> 24) & 1;                                     /* bit MT: nucleo en Aff1 (Cortex-A76 de la Pi 5) */
    __asm__ volatile ("mrs %0, mair_el1" : "=r"(smp_mmu_cfg.mair));
    __asm__ volatile ("mrs %0, tcr_el1" : "=r"(smp_mmu_cfg.tcr));
    __asm__ volatile ("mrs %0, ttbr0_el1" : "=r"(smp_mmu_cfg.ttbr0));
    __asm__ volatile ("mrs %0, sctlr_el1" : "=r"(smp_mmu_cfg.sctlr));
    if (!(smp_mmu_cfg.sctlr & 1)) { kprint("SMP: la MMU no esta activa, se sigue con un nucleo\n"); return; }
    dc_civac(&smp_mmu_cfg, sizeof smp_mmu_cfg);
    dc_civac(smp_stacks, sizeof smp_stacks);
    dc_civac(g_slot, sizeof g_slot);
    int up = 1;
    for (int c = 1; c < SMP_MAX; c++) {
        uint64_t target = aff1 ? ((uint64_t)c << 8) : (uint64_t)c;
        int64_t r = psci_cpu_on(smc, target, (uint64_t)(uintptr_t)smp_secondary_entry, (uint64_t)c);
        if (r != 0) {
            if (c == 1 && r != -2) { kprint("SMP: PSCI CPU_ON devolvio "); kprint(r == -4 ? "ya encendido" : r == -1 ? "no soportado" : "error"); kprint("\n"); }   /* -2: ese nucleo no existe */
            continue;
        }
        uint64_t t0 = timer_ticks; uint32_t guard = 200000000u;
        while (!__atomic_load_n(&g_slot[c].online, __ATOMIC_ACQUIRE) && timer_ticks - t0 < 200 && --guard) { __asm__ volatile ("yield"); }
        if (g_slot[c].online) up++;
        else { kprint("SMP: el nucleo "); kprint_dec((uint32_t)c); kprint(" no respondio\n"); }
    }
    /* los indices de trabajo van seguidos: solo se usan nucleos consecutivos desde el 1 */
    g_ncpu = 1; while (g_ncpu < SMP_MAX && g_slot[g_ncpu].online) g_ncpu++;
    g_method = smc ? "PSCI por SMC" : "PSCI por HVC";
    kprint("SMP: "); kprint_dec((uint32_t)up); kprint(up == 1 ? " nucleo en marcha" : " nucleos en marcha"); kprint(" ("); kprint(g_method);
    kprint("; el 0 lleva el sistema, el resto calcula en paralelo: 'cpus')\n");
}

int smp_cpus(void) { return g_enabled ? g_ncpu : 1; }
int smp_enabled(void) { return g_enabled; }
void smp_set_enabled(int on) { g_enabled = on ? 1 : 0; }
const char *smp_method(void) { return g_method; }

int smp_parallel_n(smp_fn fn, void *arg, int n)
{
    int cpus = smp_cpus();
    if (n > cpus) n = cpus;
    if (n < 1) n = 1;
    if (n == 1 || g_busy) { fn(0, 1, arg); return 1; }            /* sin anidar: si ya hay un reparto en marcha, todo en este nucleo */
    g_busy = 1;
    uint32_t seq[SMP_MAX];
    for (int c = 1; c < n; c++) {
        struct slot *s = &g_slot[c];
        s->fn = fn; s->arg = arg; s->idx = c; s->n = n;
        seq[c] = s->seq + 1;
        __atomic_store_n(&s->seq, seq[c], __ATOMIC_RELEASE);
    }
    __asm__ volatile ("dsb ish\n sev" ::: "memory");
    uint64_t t0 = timer_ticks;
    fn(0, n, arg);
    g_slot[0].jobs++; g_slot[0].busy += timer_ticks - t0;
    for (int c = 1; c < n; c++)
        while (__atomic_load_n(&g_slot[c].done, __ATOMIC_ACQUIRE) != seq[c]) __asm__ volatile ("yield");
    g_busy = 0;
    return n;
}
void smp_parallel(smp_fn fn, void *arg) { (void)smp_parallel_n(fn, arg, SMP_MAX); }

int smp_core_info(int i, struct smp_core_info *o)
{
    if (i < 0 || i >= SMP_MAX) return -1;
    o->online = g_slot[i].online; o->mpidr = g_slot[i].mpidr; o->jobs = g_slot[i].jobs; o->busy_ticks = g_slot[i].busy;
    return 0;
}
