/* PARCHE 060: GICv2 (GIC-400 de la Pi 5, GICv2 de QEMU virt) y temporizador generico: interrupcion de 1 kHz que incrementa timer_ticks. */
#include <stdint.h>
#include "platform.h"
#include "idt.h"
#include "console.h"

#define GICD(o) (*(volatile uint32_t *)(uintptr_t)(SOMA_GICD_BASE + (o)))
#define GICC(o) (*(volatile uint32_t *)(uintptr_t)(SOMA_GICC_BASE + (o)))
#define TIMER_PPI 30            /* temporizador fisico no seguro de EL1 */

static uint64_t tick_period;
extern volatile uint64_t timer_ticks;
extern volatile int sched_need_resched;
volatile uint64_t irq_count;

#ifdef PLATFORM_rpi5
/* PARCHE 075: latido en el LED verde (ACT) de la Pi 5 para diagnosticar sin cable serie. El LED cuelga del GPIO 9 del banco "gio_aon"
 * de la BCM2712 (0x107d517c00; DATA en +4, IODIR en +8; activo a nivel bajo), segun el arbol de dispositivos de Linux: SIN verificar en placa.
 * Parpadeo lento (1 s) = nucleo vivo con consola HDMI; rapido (5 por segundo) = nucleo vivo pero sin framebuffer; fijo = no llego al temporizador. */
extern int rpi_fb_console_active(void);
static void led_beat(uint64_t t)
{
    volatile uint32_t *data = (volatile uint32_t *)(uintptr_t)0x107d517c04ULL;
    volatile uint32_t *dir  = (volatile uint32_t *)(uintptr_t)0x107d517c08ULL;
    uint64_t half = rpi_fb_console_active() ? 500 : 100;
    if (t % half) return;
    *dir &= ~(1u << 9);                                          /* salida */
    if ((t / half) & 1) *data |= (1u << 9); else *data &= ~(1u << 9);
}
#endif

static void timer_rearm(void)
{
    __asm__ volatile ("msr cntp_tval_el0, %0" :: "r"(tick_period));
}

void gic_irq_handler(void)
{
    uint32_t iar = GICC(0x0C);                                  /* GICC_IAR: reconoce la interrupcion */
    uint32_t id = iar & 0x3FF;
    if (id >= 1020) return;                                     /* espuria */
    if (id == TIMER_PPI) { timer_ticks++; timer_rearm(); if (timer_ticks % 10 == 0) sched_need_resched = 1;
#ifdef PLATFORM_rpi5
        led_beat(timer_ticks);
#endif
    }   /* PARCHE 072: fin de rodaja (10 ms) */
    irq_count++;
    GICC(0x10) = iar;                                           /* GICC_EOIR */
}

void soma_timer_irq_init(uint32_t hz)
{
    uint64_t freq;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0) freq = 62500000;
    tick_period = freq / hz;
    GICD(0x000) = 0;                                            /* GICD_CTLR: apagado mientras se configura */
    GICD(0x100) = 1u << TIMER_PPI;                              /* GICD_ISENABLER0: PPI 30 */
    GICD(0x400 + (TIMER_PPI & ~3u)) = 0x80u << (8 * (TIMER_PPI & 3));   /* prioridad media */
    GICD(0x000) = 1;
    GICC(0x04) = 0xF0;                                          /* GICC_PMR */
    GICC(0x00) = 1;                                             /* GICC_CTLR */
    timer_rearm();
    __asm__ volatile ("msr cntp_ctl_el0, %0" :: "r"(1ULL));    /* habilitado y sin enmascarar */
    __asm__ volatile ("msr daifclr, #2");                       /* IRQ activadas */
    {   /* autocomprobacion: en ~20 ms deben llegar al menos 5 interrupciones */
        uint64_t t0, t1; __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(t0));
        do { __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(t1)); } while (irq_count < 5 && (t1 - t0) < freq / 50);
        kprint(irq_count >= 5 ? "GIC: temporizador por interrupcion OK (MMU y cachés activas)\n" : "GIC: AVISO - no llegan interrupciones del temporizador\n");
    }
}
