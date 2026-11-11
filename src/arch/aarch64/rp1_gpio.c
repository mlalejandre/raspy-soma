#include "rp1_gpio.h"
#include "rp1.h"
#include "rp1_proto.h"
#include "gpio_proto.h"
/* PARCHE 071: pines GPIO0..27 del RP1 (cabecera de 40 pines de la Pi 5). Funcion 5 = control por software (SYS_RIO). Sin verificar en placa. */
static volatile uint32_t *iob, *rio, *pads;

static void put(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; if (max) o[*p < max ? *p : max - 1] = 0; }
static void putdec(char *o, uint32_t max, uint32_t *p, uint32_t v) { char t[12]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } char r[12]; int k = 0; while (n) r[k++] = t[--n]; r[k] = 0; put(o, max, p, r); }

static inline uint32_t rd(volatile uint32_t *b, uint32_t off) { return b[off / 4]; }
static inline void wr(volatile uint32_t *b, uint32_t off, uint32_t v) { b[off / 4] = v; __asm__ volatile ("dsb sy" ::: "memory"); }

static void describe(char *o, uint32_t max, uint32_t *p, uint32_t pin)
{
    uint32_t ctrl = rd(iob, gpio_ctrl_off(pin)), pad = rd(pads, gpio_pad_off(pin));
    uint32_t f = gpio_func_of(ctrl);
    put(o, max, p, "GPIO"); putdec(o, max, p, pin); put(o, max, p, ": ");
    if (f == GPIO_FUNC_RIO) {
        int oe = (rd(rio, GPIO_RIO_OE) >> pin) & 1u;
        put(o, max, p, oe ? "salida " : "entrada ");
        put(o, max, p, "nivel "); putdec(o, max, p, oe ? (rd(rio, GPIO_RIO_OUT) >> pin) & 1u : (rd(rio, GPIO_RIO_IN) >> pin) & 1u);
    } else {
        if (f == 31) put(o, max, p, "sin funcion (null)"); else { put(o, max, p, "funcion alternativa "); putdec(o, max, p, f); }
    }
    enum gpio_pull pl = gpio_pad_pull(pad);
    put(o, max, p, pl == GPIO_PULL_UP ? ", pull-up" : pl == GPIO_PULL_DOWN ? ", pull-down" : ", sin pull");
    put(o, max, p, "\n");
}

static void make_sw(uint32_t pin)                    /* el pin pasa a control por software */
{
    wr(iob, gpio_ctrl_off(pin), gpio_ctrl_with_func(rd(iob, gpio_ctrl_off(pin)), GPIO_FUNC_RIO));
}

int rp1_gpio_run(const char *args, char *out, uint32_t max)
{
    uint32_t p = 0;
    if (max) out[0] = 0;
    if (rp1_init() != 0) { put(out, max, &p, "gpio: no hay RP1 (QEMU raspi5b no lo emula; usa SOMA_PI5_RP1=1)\n"); return -1; }
    uint64_t a = rp1_periph_base(RP1_IO_BANK0), b = rp1_periph_base(RP1_SYS_RIO0), c = rp1_periph_base(RP1_PADS_BANK0);
    if (!a || !b || !c) { put(out, max, &p, "gpio: el RP1 no expone los bloques de GPIO\n"); return -1; }
    iob = (volatile uint32_t *)(uintptr_t)a; rio = (volatile uint32_t *)(uintptr_t)b; pads = (volatile uint32_t *)(uintptr_t)c;
    struct gpio_cmd cmd;
    if (gpio_parse(args, &cmd) != 0) {
        put(out, max, &p, "uso: gpio                 lista los 28 pines\n     gpio N               estado del pin N (0..27)\n     gpio N in|out|0|1|toggle   entrada / salida (0) / nivel / invertir\n     gpio N up|down|nopull      resistencia interna\n");
        return -1;
    }
    uint32_t m = gpio_pin_mask(cmd.pin), pad;
    switch (cmd.op) {
    case GPIO_OP_LIST:
        for (uint32_t i = 0; i < GPIO_COUNT; i++) describe(out, max, &p, i);
        return 0;
    case GPIO_OP_READ: break;
    case GPIO_OP_IN:
        make_sw(cmd.pin);
        pad = rd(pads, gpio_pad_off(cmd.pin));
        wr(pads, gpio_pad_off(cmd.pin), gpio_pad_value(pad, 1, 1, gpio_pad_pull(pad)));        /* entrada habilitada, salida deshabilitada */
        wr(rio, GPIO_RIO_OE + GPIO_ALIAS_CLR, m);
        break;
    case GPIO_OP_OUT0: case GPIO_OP_OUT1: case GPIO_OP_TOGGLE:
        make_sw(cmd.pin);
        pad = rd(pads, gpio_pad_off(cmd.pin));
        wr(pads, gpio_pad_off(cmd.pin), gpio_pad_value(pad, 1, 0, GPIO_PULL_NONE));
        if (cmd.op == GPIO_OP_TOGGLE) wr(rio, GPIO_RIO_OUT + GPIO_ALIAS_XOR, m);
        else wr(rio, GPIO_RIO_OUT + (cmd.op == GPIO_OP_OUT1 ? GPIO_ALIAS_SET : GPIO_ALIAS_CLR), m);
        wr(rio, GPIO_RIO_OE + GPIO_ALIAS_SET, m);
        break;
    case GPIO_OP_PULL_UP: case GPIO_OP_PULL_DOWN: case GPIO_OP_PULL_NONE:
        pad = rd(pads, gpio_pad_off(cmd.pin));
        wr(pads, gpio_pad_off(cmd.pin), gpio_pad_value(pad, gpio_pad_ie(pad), gpio_pad_od(pad),
            cmd.op == GPIO_OP_PULL_UP ? GPIO_PULL_UP : cmd.op == GPIO_OP_PULL_DOWN ? GPIO_PULL_DOWN : GPIO_PULL_NONE));
        break;
    }
    describe(out, max, &p, cmd.pin);
    return 0;
}

/* PARCHE 077: ventilador de la Pi 5. Su senal PWM es el GPIO45 del RP1 (banco 2, pin 11 del banco; bloques IO_BANK2/SYS_RIO2/PADS_BANK2 a +0x8000 de
 * los del banco 0). Sin controlador PWM: el pin se maneja como salida fija = ventilador al 100 % o parado. En Linux el PWM del ventilador es de
 * polaridad invertida, asi que "en marcha" = nivel bajo. SIN verificar en placa: 'fan inv' invierte el nivel si gira al reves de lo esperado. */
static int fan_inv, fan_state = -1;
int rp1_fan_set(int on)
{
    if (rp1_init() != 0) return -1;
    uint64_t a = rp1_periph_base(RP1_IO_BANK0), b = rp1_periph_base(RP1_SYS_RIO0), c = rp1_periph_base(RP1_PADS_BANK0);
    if (!a || !b || !c) return -1;
    volatile uint32_t *io2 = (volatile uint32_t *)(uintptr_t)(a + 0x8000), *rio2 = (volatile uint32_t *)(uintptr_t)(b + 0x8000), *pad2 = (volatile uint32_t *)(uintptr_t)(c + 0x8000);
    const uint32_t pin = 11, m = 1u << pin;
    int level = on ? 0 : 1;
    if (fan_inv) level = !level;
    wr(rio2, GPIO_RIO_OUT + (level ? GPIO_ALIAS_SET : GPIO_ALIAS_CLR), m);              /* primero el nivel, luego la salida: sin pulso intermedio */
    wr(pad2, gpio_pad_off(pin), gpio_pad_value(rd(pad2, gpio_pad_off(pin)), 1, 0, GPIO_PULL_NONE));
    wr(rio2, GPIO_RIO_OE + GPIO_ALIAS_SET, m);
    wr(io2, gpio_ctrl_off(pin), gpio_ctrl_with_func(rd(io2, gpio_ctrl_off(pin)), GPIO_FUNC_RIO));
    fan_state = on ? 1 : 0;
    return 0;
}
int rp1_fan_state(void) { return fan_state; }
int rp1_fan_inverted(void) { return fan_inv; }
void rp1_fan_invert(void) { fan_inv = !fan_inv; if (fan_state >= 0) (void)rp1_fan_set(fan_state); }
