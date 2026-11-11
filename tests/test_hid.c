/* PARCHE 047: pruebas de hid_proto (teclado -> scancodes PS/2) y de las nuevas piezas de xhci_proto. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/hid_proto.h"
#include "../src/core/xhci_proto.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    uint8_t o[64], z[8] = {0}, r[8];
    /* 'a' (usage 4): pulsar y soltar */
    memset(r, 0, 8); r[2] = 4;
    CHECK(hid_kbd_diff(z, r, o, 64) == 1 && o[0] == 0x1E);
    CHECK(hid_kbd_diff(r, z, o, 64) == 1 && o[0] == 0x9E);
    /* Mayus + 'a', luego soltar todo: orden mod, tecla / tecla, mod */
    uint8_t s[8] = {2, 0, 4, 0, 0, 0, 0, 0};
    CHECK(hid_kbd_diff(z, s, o, 64) == 2 && o[0] == 0x2A && o[1] == 0x1E);
    CHECK(hid_kbd_diff(s, z, o, 64) == 2 && o[0] == 0x9E && o[1] == 0xAA);
    /* flecha arriba = E0 48; soltar = E0 C8 */
    memset(r, 0, 8); r[2] = 0x52;
    CHECK(hid_kbd_diff(z, r, o, 64) == 2 && o[0] == 0xE0 && o[1] == 0x48);
    CHECK(hid_kbd_diff(r, z, o, 64) == 2 && o[0] == 0xE0 && o[1] == 0xC8);
    /* Ctrl derecho (E0 1D) y rollover */
    uint8_t cr[8] = {0x10, 0, 0, 0, 0, 0, 0, 0};
    CHECK(hid_kbd_diff(z, cr, o, 64) == 2 && o[0] == 0xE0 && o[1] == 0x1D);
    uint8_t ov[8] = {0, 0, 1, 1, 1, 1, 1, 1};
    CHECK(hid_kbd_diff(z, ov, o, 64) == 0);
    /* dos teclas a la vez, una se suelta y otra se mantiene */
    uint8_t ab[8] = {0, 0, 4, 5, 0, 0, 0, 0}, b_[8] = {0, 0, 5, 0, 0, 0, 0, 0};
    CHECK(hid_kbd_diff(z, ab, o, 64) == 2);
    CHECK(hid_kbd_diff(ab, b_, o, 64) == 1 && o[0] == 0x9E);
    /* limite de salida: nunca se parte un E0 xx ni se escribe mas de max */
    uint8_t many[8] = {0xFF, 0, 0x4F, 0x50, 0x51, 0x52, 0x4A, 0x4B};
    for (int mx = 0; mx <= 40; mx++) { uint8_t big[64]; memset(big, 0xAA, sizeof big); int n = hid_kbd_diff(z, many, big, mx); CHECK(n <= mx); if (n) CHECK(big[n - 1] != 0xE0); CHECK(big[mx < 64 ? mx : 63] == 0xAA || mx >= 63); }
    /* todos los usages 0..255 y teclas aleatorias no se salen de la tabla */
    for (int u = 0; u < 256; u++) { uint8_t t[2]; int n = hid_usage_to_set1((uint8_t)u, t); CHECK(n >= 0 && n <= 2); }
    srand(3);
    for (int it = 0; it < 50000; it++) { uint8_t a[8], c[8], b[64]; for (int k = 0; k < 8; k++) { a[k] = (uint8_t)rand(); c[k] = (uint8_t)rand(); } hid_kbd_diff(a, c, b, 1 + rand() % 64); }
    /* raton */
    struct hid_mouse m; uint8_t mr[4] = {1, 0xFE, 5, 0xFF};
    CHECK(hid_mouse_parse(mr, 4, &m) == 1 && m.buttons == 1 && m.dx == -2 && m.dy == 5 && m.wheel == -1);
    CHECK(hid_mouse_parse(mr, 3, &m) == 1 && m.wheel == 0 && hid_mouse_parse(mr, 2, &m) == 0);
    /* intervalos de extremo */
    CHECK(xhci_ep_interval(1, 10) == 6 && xhci_ep_interval(2, 10) == 6 && xhci_ep_interval(1, 1) == 3 && xhci_ep_interval(1, 255) == 10);
    CHECK(xhci_ep_interval(3, 4) == 3 && xhci_ep_interval(4, 0) == 0 && xhci_ep_interval(3, 255) == 15);
    /* Configure Endpoint */
    for (int csz = 0; csz < 2; csz++) {
        uint32_t cs = csz ? 64 : 32; uint8_t *out = calloc(1, cs), *ctx = malloc(33 * cs);
        ((uint32_t *)out)[0] = (1u << 27) | (1u << 20); ((uint32_t *)out)[1] = 5u << 16;
        xhci_build_cfgep_input(ctx, csz, out, 3, 8, 6, 0x7654000ULL);
        uint32_t *icc = (uint32_t *)ctx, *sl = (uint32_t *)(ctx + cs), *ep = (uint32_t *)(ctx + 4 * cs);
        CHECK(icc[0] == 0 && icc[1] == (1u | 8u) && (sl[0] >> 27) == 3 && ((sl[0] >> 20) & 0xF) == 1 && (sl[1] >> 16) == 5);
        CHECK(ep[0] == 6u << 16 && ((ep[1] >> 3) & 7) == 7 && (ep[1] >> 16) == 8 && ep[2] == (0x7654000u | 1) && ep[4] == (8u | 8u << 16));
        free(out); free(ctx);
    }
    /* Configure Endpoint de dos extremos bulk */
    {
        uint32_t cs = 32; uint8_t *out = calloc(1, cs), *ctx = malloc(33 * cs);
        xhci_build_cfg_bulk_input(ctx, 0, out, 3, 4, 512, 0x1000, 0x2000);
        uint32_t *icc = (uint32_t *)ctx, *sl = (uint32_t *)(ctx + cs), *epi = (uint32_t *)(ctx + 4 * cs), *epo = (uint32_t *)(ctx + 5 * cs);
        CHECK(icc[1] == (1u | 8u | 16u) && (sl[0] >> 27) == 4);
        CHECK(((epi[1] >> 3) & 7) == 6 && ((epo[1] >> 3) & 7) == 2 && (epi[1] >> 16) == 512 && epi[2] == 0x1001 && epo[2] == 0x2001 && epi[4] == 3072);
        free(out); free(ctx);
    }
    /* hubs: ruta, TT, descriptor y estado de puerto */
    for (int csz = 0; csz < 2; csz++) {
        uint32_t cs = csz ? 64 : 32; uint8_t *ctx = malloc(33 * cs), *out = calloc(1, cs);
        xhci_build_addr_input_ex(ctx, csz, 1, 0x00321, 4, 8, 0x5000, 7, 2);
        uint32_t *sl = (uint32_t *)(ctx + cs);
        CHECK((sl[0] & 0xFFFFF) == 0x321 && ((sl[0] >> 20) & 0xF) == 1 && (sl[0] >> 27) == 1 && (sl[1] >> 16) == 4 && sl[2] == (7u | 2u << 8));
        xhci_build_addr_input_ex(ctx, csz, 3, 0x1, 4, 64, 0x5000, 0, 0);
        CHECK(((uint32_t *)(ctx + cs))[2] == 0 && (((uint32_t *)(ctx + cs))[0] & 0xFFFFF) == 1);
        ((uint32_t *)out)[0] = (3u << 20) | (1u << 27); ((uint32_t *)out)[1] = 5u << 16;
        xhci_build_hub_input(ctx, csz, out, 7, 2, 0);
        sl = (uint32_t *)(ctx + cs);
        CHECK(((uint32_t *)ctx)[1] == 1 && (sl[0] & (1u << 26)) && !(sl[0] & (1u << 25)) && (sl[1] >> 24) == 7 && ((sl[1] >> 16) & 0xFF) == 5 && ((sl[2] >> 16) & 3) == 2 && ((sl[0] >> 20) & 0xF) == 3);
        free(ctx); free(out);
    }
    { struct usb_hub_desc h; uint8_t hd[9] = { 9, 0x29, 4, 0xA9, 0x00, 50, 100, 0, 0xFF };
      CHECK(usb_parse_hub_desc(hd, 9, &h) == 0 && h.nports == 4 && h.tt_think == 1 && h.power_on_ms_div2 == 50 && h.chars == 0xA9);
      hd[1] = 0x2A; CHECK(usb_parse_hub_desc(hd, 9, &h) != 0); hd[1] = 0x29; hd[2] = 0; CHECK(usb_parse_hub_desc(hd, 9, &h) != 0);
      hd[2] = 16; CHECK(usb_parse_hub_desc(hd, 9, &h) != 0); hd[2] = 4; hd[0] = 11; CHECK(usb_parse_hub_desc(hd, 9, &h) == 0 && h.nports == 4); hd[0] = 5; CHECK(usb_parse_hub_desc(hd, 9, &h) != 0); CHECK(usb_parse_hub_desc(hd, 5, &h) != 0); }
    { struct usb_port_status ps; usb_decode_port_status(0x00110103u | (1u << 10), &ps);
      CHECK(ps.connected && ps.enabled && !ps.reset && ps.power && ps.high && !ps.low && ps.c_conn && ps.c_reset); }
    /* control sin datos */
    {
        struct xhci_trb *mem = aligned_alloc(64, 16 * 16); struct xhci_ring rg; uint64_t last = 0, base = (uint64_t)(uintptr_t)mem;
        xhci_ring_init(&rg, mem, 16, base);
        xhci_ctrl_nodata(&rg, base, 0x21, 0x0B, 0, 2, &last);
        CHECK(TRB_GET_TYPE(mem[0].d3) == TRB_SETUP && ((mem[0].d3 >> 16) & 3) == 0 && mem[0].d0 == (0x21u | 0x0Bu << 8) && mem[0].d1 == 2);
        CHECK(TRB_GET_TYPE(mem[1].d3) == TRB_STATUS && (mem[1].d3 & (1 << 16)) && (mem[1].d3 & (1 << 5)) && last == base + 16);
        free(mem);
    }
    printf(fails ? "hid_proto: %d FALLOS\n" : "hid_proto: todo correcto\n", fails);
    return fails != 0;
}
