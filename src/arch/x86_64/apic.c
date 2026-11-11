/* PARCHE 037: ver apic.h. Solo lecturas (la unica escritura es el selector de registro del IOAPIC, que no tiene efectos). */
#include "apic.h"
#include "vmm.h"
#include "idt.h"

#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)

static uint32_t lapic_base, io_base;                       /* direccion MMIO ya mapeada por apic_probe */
static volatile uint32_t apic_irqs;

static int map_mmio(uint32_t phys)
{
    static uint32_t done[4]; static int n;
    for (int i = 0; i < n; i++) if (done[i] == phys) return 0;
    if (phys < 0xFE000000u || (phys & 0xFFF) || n >= 4) return -1;          /* ventana MMIO estandar de la plataforma */
    if (vmm_map_page(phys, phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | PTE_PWT | PTE_PCD) != 0) return -1;
    done[n++] = phys;
    return 0;
}

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (uint64_t)hi << 32 | lo;
}

static uint32_t io_read(uint32_t base, uint32_t reg)
{
    volatile uint32_t *sel = (volatile uint32_t *)(uintptr_t)base, *win = (volatile uint32_t *)(uintptr_t)(base + 0x10);
    *sel = reg;
    return *win;
}

int apic_probe(uint32_t lapic_phys, uint32_t ioapic_phys, struct apic_info *o)
{
    for (unsigned i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    uint32_t a = 1, b, c, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    o->cpu_has_apic = (d >> 9) & 1;
    if (!o->cpu_has_apic) return -1;
    o->msr_base = rdmsr(0x1B);
    if (map_mmio(lapic_phys) == 0) {
        lapic_base = lapic_phys;
        volatile uint32_t *l = (volatile uint32_t *)(uintptr_t)lapic_phys;
        o->lapic_id = l[0x20 / 4] >> 24; o->lapic_ver = l[0x30 / 4]; o->lapic_tpr = l[0x80 / 4]; o->lapic_svr = l[0xF0 / 4];
        o->lapic_ok = 1;
    }
    if (ioapic_phys && map_mmio(ioapic_phys) == 0) {
        o->io_id = io_read(ioapic_phys, 0) >> 24; o->io_ver = io_read(ioapic_phys, 1);
        int n = (int)((o->io_ver >> 16) & 0xFF) + 1; if (n > 24) n = 24;
        for (int i = 0; i < n; i++) o->redir[i] = (uint64_t)io_read(ioapic_phys, 0x11 + 2u * i) << 32 | io_read(ioapic_phys, 0x10 + 2u * i);
        o->nredir = n; o->ioapic_ok = 1; io_base = ioapic_phys;
    }
    return o->lapic_ok ? 0 : -1;
}

/* ---- PARCHE 038: autotest del temporizador del LAPIC ---- */
#define L_EOI 0xB0
#define L_LVT_TIMER 0x320
#define L_TIMER_INIT 0x380
#define L_TIMER_CUR 0x390
#define L_TIMER_DIV 0x3E0
#define APIC_TEST_VECTOR 64

static inline uint32_t lrd(uint32_t off) { return *(volatile uint32_t *)(uintptr_t)(lapic_base + off); }
static inline void lwr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(lapic_base + off) = v; }

void apic_timer_irq(void)
{
    apic_irqs++;
    if (lapic_base) lwr(L_EOI, 0);
}

int apic_timer_selftest(struct apic_test *t)
{
    t->ticks_per_ms = t->irq_wait_ms = t->irqs = 0;
    if (!lapic_base) return -1;
    uint64_t fl; __asm__ volatile ("pushfq; popq %0" : "=r"(fl));
    if (!(fl & 0x200)) return -2;
    lwr(L_LVT_TIMER, 0x10000 | APIC_TEST_VECTOR);                 /* enmascarado mientras se configura */
    lwr(L_TIMER_DIV, 0x3);                                         /* divisor 16 */
    /* calibracion: 20 ticks del PIT (1 kHz) = 20 ms; se espera a un flanco para no medir un tick parcial */
    uint64_t t0 = timer_ticks; unsigned long guard = 400000000UL;
    while (timer_ticks == t0 && --guard) __asm__ volatile ("pause");
    if (!guard) return -3;
    t0 = timer_ticks;
    lwr(L_TIMER_INIT, 0xFFFFFFFFu);
    guard = 400000000UL;
    while (timer_ticks - t0 < 20 && --guard) __asm__ volatile ("pause");
    uint32_t cur = lrd(L_TIMER_CUR);
    lwr(L_TIMER_INIT, 0);
    if (!guard) return -3;
    uint64_t elapsed = 0xFFFFFFFFu - cur;
    t->ticks_per_ms = (uint32_t)(elapsed / 20);
    if (t->ticks_per_ms < 100 || t->ticks_per_ms > 4000000u) return -4;      /* 100 kHz .. 4 GHz a divisor 16: fuera de lo creible */
    /* entrega: una interrupcion de un disparo a ~5 ms */
    apic_irqs = 0;
    lwr(L_LVT_TIMER, APIC_TEST_VECTOR);                                       /* un disparo, sin mascara */
    t0 = timer_ticks;
    lwr(L_TIMER_INIT, t->ticks_per_ms * 5);
    guard = 400000000UL;
    while (!apic_irqs && timer_ticks - t0 < 100 && --guard) __asm__ volatile ("pause");
    t->irq_wait_ms = (uint32_t)(timer_ticks - t0);
    lwr(L_LVT_TIMER, 0x10000 | APIC_TEST_VECTOR);                             /* vuelve a enmascarar */
    lwr(L_TIMER_INIT, 0);
    t->irqs = apic_irqs;
    return apic_irqs ? 0 : -5;
}

/* ---- PARCHE 039: enrutado por IOAPIC ---- */
static inline void poutb_(uint16_t port, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline uint8_t inb_(uint16_t port) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static void io_write(uint32_t reg, uint32_t v)
{
    volatile uint32_t *sel = (volatile uint32_t *)(uintptr_t)io_base, *win = (volatile uint32_t *)(uintptr_t)(io_base + 0x10);
    *sel = reg; *win = v;
}

static int route_active, route_gsi[2];
static uint8_t pic_saved_bits;                  /* bits 0 y 1 de la mascara del PIC maestro antes de activar el enrutado */
static volatile uint32_t eoi_count;

int apic_routed(void) { return route_active; }
uint32_t apic_eoi_count(void) { return eoi_count; }
void apic_eoi(void)
{
    if (!route_active) return;
    eoi_count++;
    lwr(L_EOI, 0);
}

int apic_route_on(const struct apic_route_cfg *c)
{
    if (!lapic_base || !io_base) return -1;
    if (route_active) return -2;
    uint64_t fl; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(fl) : : "memory");
    for (int i = 0; i < 2; i++) {
        uint32_t lo = (uint32_t)(32 + i) | (c->low[i] ? 1u << 13 : 0) | (c->level[i] ? 1u << 15 : 0);     /* fijo, destino fisico, sin mascara */
        route_gsi[i] = c->gsi[i];
        io_write(0x11 + 2u * (uint32_t)c->gsi[i], (uint32_t)c->lapic_id << 24);
        io_write(0x10 + 2u * (uint32_t)c->gsi[i], lo);
    }
    route_active = 1; eoi_count = 0;
    uint8_t m = inb_(0x21); pic_saved_bits = m & 3;
    poutb_(0x21, (uint8_t)(m | 3));                                  /* el PIC deja de entregar IRQ0/IRQ1 */
    if (fl & 0x200) __asm__ volatile ("sti" : : : "memory");
    return 0;
}

void apic_route_off(void)
{
    if (!route_active) return;
    uint64_t fl; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(fl) : : "memory");
    uint8_t m = inb_(0x21);
    poutb_(0x21, (uint8_t)((m & ~3u) | pic_saved_bits));
    for (int i = 0; i < 2; i++) io_write(0x10 + 2u * (uint32_t)route_gsi[i], 0x10000u | (uint32_t)(32 + i));   /* enmascarada */
    route_active = 0;
    if (fl & 0x200) __asm__ volatile ("sti" : : : "memory");
}
