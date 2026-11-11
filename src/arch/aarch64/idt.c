#include "idt.h"
#include "console.h"
#include "arch.h"
#include "io.h"
#include "panic.h"

volatile int boot_gate_active = 0;
volatile uint64_t timer_ticks = 0;

void tss_init(void) {}
void idt_init(void) {}

extern void gic_irq_handler(void);

void arm64_exception_handler(uint64_t type, uint64_t *regs)
{
    if (type == 5 || type == 1 || type == 9) { gic_irq_handler(); return; }      /* PARCHE 060: IRQ (EL1h, EL1t, EL0) */
    uint64_t esr, far, elr, spsr;
    __asm__ volatile ("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile ("mrs %0, far_el1" : "=r"(far));
    __asm__ volatile ("mrs %0, elr_el1" : "=r"(elr));
    __asm__ volatile ("mrs %0, spsr_el1" : "=r"(spsr));

    kprint("\n\n========================================================================\n");
    kprint("!!! KERNEL PANIC: EXCEPCION CPU AARCH64 DETECTADA !!!\n");
    kprint("========================================================================\n");
    kprint("Vector Type: "); kprint_dec((uint32_t)type);
    kprint(" | SPSR_EL1: 0x"); kprint_hex32((uint32_t)spsr); kprint("\n");
    kprint("ELR_EL1 (PC de la instruccion que fallo): "); panic_hex64(elr); kprint("\n");
    kprint("ESR_EL1 (Sindrome de Excepcion):          0x"); kprint_hex32((uint32_t)esr); kprint("\n");
    kprint("FAR_EL1 (Direccion de Memoria fallida):   "); panic_hex64(far); kprint("\n");
    kprint("SP actual:                                0x"); kprint_hex32((uint32_t)(uintptr_t)regs); kprint("\n");
    panic_print_context(elr, regs[30], regs[29], (uint64_t)(uintptr_t)regs);   /* SOMA_PARCHE_008_PANIC */
    kprint("========================================================================\n");
    kprint("Sistema detenido por proteccion (HLT/WFI).\n");
    {
        /* PARCHE 046: panic observable por el host (mismos codigos que x86_64). */
        static volatile int exiting = 0;
        if (!exiting) {
            exiting = 1;
            if (boot_gate_active) qemu_exit(0x22);
            qemu_exit(0x23);
        }
    }
    for (;;) {
        arch_halt();
    }
}

uint64_t timer_get_uptime_ms(void)
{
    uint64_t count, freq;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(count));
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0) freq = 62500000;
    return (count * 1000) / freq;
}

void timer_sleep_ms(uint32_t ms)
{
    uint64_t start = timer_get_uptime_ms();
    while ((timer_get_uptime_ms() - start) < ms) {
        arch_pause();
    }
}

void pic_remap(void) {}
extern void soma_timer_irq_init(uint32_t hz);
void timer_init(uint32_t freq_hz)
{
    soma_timer_irq_init(freq_hz);       /* PARCHE 060: GIC + temporizador por interrupcion */
}
