#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/rp1_proto.h"
int main(void)
{
    assert(rp1_is_rp1(0x1de4, 0x0001) && !rp1_is_rp1(0x1de4, 2) && !rp1_is_rp1(0x8086, 1));
    assert(rp1_periph_off(RP1_SYSINFO) == 0 && rp1_periph_off(RP1_ETH) == 0x100000);
    assert(rp1_periph_off(RP1_USB0) == 0x200000 && rp1_periph_off(RP1_USB1) == 0x300000);
    assert(rp1_periph_off(RP1_SYS_RIO0) == 0xE0000 && rp1_periph_off(RP1_PADS_BANK0) == 0xF0000);
    assert(rp1_periph_off(RP1_IO_BANK0) == 0xD0000 && rp1_periph_off(-1) == -1 && rp1_periph_off(RP1_PERIPH_COUNT) == -1);
    assert(rp1_periph_len(99) == 0 && !strcmp(rp1_periph_name(RP1_USB1), "usbhost1") && !strcmp(rp1_periph_name(-5), "?"));
    /* ningun bloque se solapa ni sale de los 4 MiB */
    for (int a = 0; a < RP1_PERIPH_COUNT; a++) {
        assert((uint64_t)rp1_periph_off(a) + rp1_periph_len(a) <= RP1_BAR1_MIN);
        for (int b = a + 1; b < RP1_PERIPH_COUNT; b++) {
            uint64_t sa = (uint64_t)rp1_periph_off(a), ea = sa + rp1_periph_len(a), sb = (uint64_t)rp1_periph_off(b), eb = sb + rp1_periph_len(b);
            assert(ea <= sb || eb <= sa);
        }
    }
    assert(rp1_periph_cpu(0x1f00100000ULL, 0x400000, RP1_USB0) == 0x1f00300000ULL);
    assert(rp1_periph_cpu(0x1f00100000ULL, 0x200000, RP1_USB0) == 0);              /* la BAR no llega */
    assert(rp1_periph_cpu(0, 0x400000, RP1_USB0) == 0 && rp1_periph_cpu(1, 0x400000, 77) == 0);
    puts("rp1_proto: todo correcto");
    return 0;
}
