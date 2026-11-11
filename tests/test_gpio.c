#include <assert.h>
#include <stdio.h>
#include "../src/core/gpio_proto.h"
int main(void)
{
    assert(gpio_valid(0) && gpio_valid(27) && !gpio_valid(28));
    assert(gpio_status_off(0) == 0 && gpio_ctrl_off(0) == 4 && gpio_ctrl_off(27) == 27 * 8 + 4 && gpio_pad_off(0) == 4 && gpio_pad_off(27) == 112);
    assert(gpio_ctrl_with_func(0xFFFFFFE0u | 3u, GPIO_FUNC_RIO) == (0xFFFFFFE0u | 5u) && gpio_func_of(0x1235) == 0x15);
    uint32_t v = gpio_pad_value(0x57, 1, 0, GPIO_PULL_UP);
    assert(gpio_pad_ie(v) && !gpio_pad_od(v) && gpio_pad_pull(v) == GPIO_PULL_UP && (v & 0x13) == (0x57 & 0x13));
    v = gpio_pad_value(v, 0, 1, GPIO_PULL_DOWN);
    assert(!gpio_pad_ie(v) && gpio_pad_od(v) && gpio_pad_pull(v) == GPIO_PULL_DOWN && !(v & (1u << 3)));
    assert(gpio_pad_pull(gpio_pad_value(0xFF, 1, 0, GPIO_PULL_NONE)) == GPIO_PULL_NONE);
    assert(gpio_pin_mask(5) == 32u && gpio_pin_mask(40) == 0);
    struct gpio_cmd c;
    assert(gpio_parse("", &c) == 0 && c.op == GPIO_OP_LIST && gpio_parse(NULL, &c) == 0 && c.op == GPIO_OP_LIST && gpio_parse("   ", &c) == 0 && c.op == GPIO_OP_LIST);
    assert(gpio_parse("17", &c) == 0 && c.op == GPIO_OP_READ && c.pin == 17);
    assert(gpio_parse(" 4  in ", &c) == 0 && c.op == GPIO_OP_IN && c.pin == 4);
    assert(gpio_parse("4 out", &c) == 0 && c.op == GPIO_OP_OUT0 && gpio_parse("4 0", &c) == 0 && c.op == GPIO_OP_OUT0 && gpio_parse("4 1", &c) == 0 && c.op == GPIO_OP_OUT1);
    assert(gpio_parse("27 toggle", &c) == 0 && c.op == GPIO_OP_TOGGLE && c.pin == 27);
    assert(gpio_parse("2 up", &c) == 0 && c.op == GPIO_OP_PULL_UP && gpio_parse("2 down", &c) == 0 && c.op == GPIO_OP_PULL_DOWN && gpio_parse("2 nopull", &c) == 0 && c.op == GPIO_OP_PULL_NONE);
    assert(gpio_parse("28", &c) == -1 && gpio_parse("x", &c) == -1 && gpio_parse("4 inx", &c) == -1 && gpio_parse("4 in extra", &c) == -1 && gpio_parse("4 2", &c) == -1 && gpio_parse("99999999999", &c) == -1);
    puts("gpio_proto: todo correcto");
    return 0;
}
