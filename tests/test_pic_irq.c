/* Pruebas en host de la decision sobre IRQ del PIC 8259 (src/arch/x86_64/pic_irq.h): espurias frente a reales, exhaustivo. */
#include <stdio.h>
#include <stdint.h>
#include "soma_test.h"
#include "pic_irq.h"

static void test_espurias(void)
{
    CHECK_EQ_INT(pic_irq_action(39, 0x00, 0x00), PIC_IRQ_SPURIOUS_NO_EOI);        /* IRQ7 sin bit 7 en ISR: espuria */
    CHECK_EQ_INT(pic_irq_action(39, 0x7F, 0xFF), PIC_IRQ_SPURIOUS_NO_EOI);        /* el esclavo no cuenta para la IRQ7 */
    CHECK_EQ_INT(pic_irq_action(39, 0x80, 0x00), PIC_IRQ_ACK_MASTER);             /* IRQ7 real: EOI normal */
    CHECK_EQ_INT(pic_irq_action(47, 0xFF, 0x00), PIC_IRQ_SPURIOUS_ACK_MASTER);    /* IRQ15 espuria: EOI solo al maestro */
    CHECK_EQ_INT(pic_irq_action(47, 0x00, 0x7F), PIC_IRQ_SPURIOUS_ACK_MASTER);
    CHECK_EQ_INT(pic_irq_action(47, 0x00, 0x80), PIC_IRQ_ACK_BOTH);               /* IRQ15 real: EOI a los dos */
}
static void test_exhaustivo(void)
{
    for (unsigned v = 0; v < 256; ++v)
        for (unsigned m = 0; m < 256; m += 1)
            for (unsigned s = 0; s < 256; s += 5) {
                enum pic_irq_action a = pic_irq_action(v, (uint8_t)m, (uint8_t)s);
                if (v < 32 || v > 47) { CHECK_EQ_INT(a, PIC_IRQ_NOT_PIC); continue; }
                if (v != 39 && v != 47) {                           /* solo 7 y 15 pueden ser espurias */
                    CHECK_EQ_INT(a, v >= 40 ? PIC_IRQ_ACK_BOTH : PIC_IRQ_ACK_MASTER);
                    continue;
                }
                if (v == 39) CHECK_EQ_INT(a, (m & 0x80) ? PIC_IRQ_ACK_MASTER : PIC_IRQ_SPURIOUS_NO_EOI);
                else         CHECK_EQ_INT(a, (s & 0x80) ? PIC_IRQ_ACK_BOTH : PIC_IRQ_SPURIOUS_ACK_MASTER);
                if (soma_t_fails > 20) return;
            }
}
int main(void)
{
    test_espurias(); test_exhaustivo();
    TEST_MAIN_END("pic_irq");
}
