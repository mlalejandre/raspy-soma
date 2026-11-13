/* PARCHE 108 (Fase 1, seguridad): decision pura sobre una IRQ del PIC 8259, sin dependencias del kernel para poder probarla en host.
 * El 8259 puede entregar una IRQ ESPURIA (vector 39 = IRQ7 del maestro, 47 = IRQ15 del esclavo) aunque la linea este enmascarada: la CPU
 * recibe el vector pero el bit correspondiente del registro ISR no esta activo. Una espuria NO se confirma con EOI (la del maestro) o
 * se confirma SOLO al maestro (la del esclavo, porque la cascada IRQ2 si quedo en servicio). Confirmar de mas puede perder una IRQ real. */
#ifndef SOMA_PIC_IRQ_H
#define SOMA_PIC_IRQ_H
#include <stdint.h>

enum pic_irq_action {
    PIC_IRQ_ACK_MASTER = 1,        /* IRQ real del maestro (vector 32..39): EOI al maestro */
    PIC_IRQ_ACK_BOTH = 2,          /* IRQ real del esclavo (vector 40..47): EOI al esclavo y al maestro */
    PIC_IRQ_SPURIOUS_NO_EOI = 3,   /* espuria del maestro (IRQ7): sin EOI */
    PIC_IRQ_SPURIOUS_ACK_MASTER = 4, /* espuria del esclavo (IRQ15): EOI solo al maestro */
    PIC_IRQ_NOT_PIC = 0            /* el vector no es de una IRQ del PIC */
};

/* isr_master/isr_slave: registros ISR leidos con OCW3 (solo se necesitan para los vectores 39 y 47). */
static inline enum pic_irq_action pic_irq_action(unsigned vector, uint8_t isr_master, uint8_t isr_slave)
{
    if (vector < 32 || vector > 47) return PIC_IRQ_NOT_PIC;
    if (vector == 39 && !(isr_master & 0x80)) return PIC_IRQ_SPURIOUS_NO_EOI;
    if (vector == 47 && !(isr_slave & 0x80)) return PIC_IRQ_SPURIOUS_ACK_MASTER;
    return vector >= 40 ? PIC_IRQ_ACK_BOTH : PIC_IRQ_ACK_MASTER;
}
#endif
