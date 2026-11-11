#include "gpio_proto.h"
/* PARCHE 071 */
#define PAD_PDE (1u << 2)
#define PAD_PUE (1u << 3)
#define PAD_IE  (1u << 6)
#define PAD_OD  (1u << 7)
int gpio_valid(uint32_t pin) { return pin < GPIO_COUNT; }
uint32_t gpio_status_off(uint32_t pin) { return pin * 8u; }
uint32_t gpio_ctrl_off(uint32_t pin) { return pin * 8u + 4u; }
uint32_t gpio_pad_off(uint32_t pin) { return 4u + pin * 4u; }
uint32_t gpio_ctrl_with_func(uint32_t ctrl, uint32_t func) { return (ctrl & ~0x1Fu) | (func & 0x1Fu); }
uint32_t gpio_func_of(uint32_t ctrl) { return ctrl & 0x1Fu; }
uint32_t gpio_pad_value(uint32_t old, int ie, int od, enum gpio_pull pull)
{
    uint32_t v = old & ~(PAD_IE | PAD_OD | PAD_PUE | PAD_PDE);
    if (ie) v |= PAD_IE;
    if (od) v |= PAD_OD;
    if (pull == GPIO_PULL_UP) v |= PAD_PUE;
    if (pull == GPIO_PULL_DOWN) v |= PAD_PDE;
    return v;
}
int gpio_pad_ie(uint32_t pad) { return (pad & PAD_IE) != 0; }
int gpio_pad_od(uint32_t pad) { return (pad & PAD_OD) != 0; }
enum gpio_pull gpio_pad_pull(uint32_t pad) { return (pad & PAD_PUE) ? GPIO_PULL_UP : (pad & PAD_PDE) ? GPIO_PULL_DOWN : GPIO_PULL_NONE; }
uint32_t gpio_pin_mask(uint32_t pin) { return pin < 32u ? 1u << pin : 0u; }

static const char *skip(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }
static int word_is(const char *s, const char *w, const char **end)
{
    const char *p = s;
    while (*w) { if (*p != *w) return 0; p++; w++; }
    if (*p && *p != ' ' && *p != '\t') return 0;
    *end = p;
    return 1;
}
int gpio_parse(const char *args, struct gpio_cmd *o)
{
    const char *s = skip(args ? args : "");
    o->op = GPIO_OP_LIST; o->pin = 0;
    if (!*s) return 0;
    if (*s < '0' || *s > '9') return -1;
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (uint32_t)(*s - '0'); if (n > 1000) return -1; s++; }
    if (!gpio_valid(n)) return -1;
    o->pin = n;
    o->op = GPIO_OP_READ;
    s = skip(s);
    if (!*s) return 0;
    const char *e;
    if (word_is(s, "in", &e)) o->op = GPIO_OP_IN;
    else if (word_is(s, "out", &e) || word_is(s, "0", &e)) o->op = GPIO_OP_OUT0;
    else if (word_is(s, "1", &e)) o->op = GPIO_OP_OUT1;
    else if (word_is(s, "toggle", &e)) o->op = GPIO_OP_TOGGLE;
    else if (word_is(s, "up", &e)) o->op = GPIO_OP_PULL_UP;
    else if (word_is(s, "down", &e)) o->op = GPIO_OP_PULL_DOWN;
    else if (word_is(s, "nopull", &e)) o->op = GPIO_OP_PULL_NONE;
    else return -1;
    return *skip(e) ? -1 : 0;
}
