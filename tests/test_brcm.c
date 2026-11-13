/* PARCHE 081: pruebas del protocolo del firmware Broadcom (SDPCM, BCDC, iovars, escaneo, claves, CLM, eventos) y de DHCP. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/brcm_proto.h"
#include "../src/net/dhcp_proto.h"

int main(void)
{
    uint8_t b[600];
    /* SDPCM */
    {
        struct sdpcm s;
        sdpcm_build(b, 40, 7, SDPCM_CH_CTRL);
        assert(b[0] == 40 && b[1] == 0 && b[2] == (uint8_t)~40 && b[3] == 0xFF && b[4] == 7 && b[5] == 0 && b[7] == 12);
        b[9] = 9;
        assert(sdpcm_parse(b, 40, &s) == 0 && s.len == 40 && s.seq == 7 && s.chan == 0 && s.doff == 12 && s.credit == 9);
        b[2] ^= 1; assert(sdpcm_parse(b, 40, &s) == -1); b[2] ^= 1;
        b[7] = 50; assert(sdpcm_parse(b, 40, &s) == -2); b[7] = 12;
        uint8_t z[12] = { 0 }; assert(sdpcm_parse(z, 12, &s) == 0 && s.len == 0);
        assert(sdpcm_can_send(10, 5) && !sdpcm_can_send(5, 5) && !sdpcm_can_send(5, 10) && sdpcm_can_send(2, 250));
    }
    /* BCDC */
    {
        size_t n = bcdc_build(b, WLC_SET_VAR, 1, 0x1234, "abc", 3);
        assert(n == 19 && b[0] == 0x07 && b[1] == 0x01 && b[4] == 3 && b[8] == 2 && b[10] == 0x34 && b[11] == 0x12 && !memcmp(b + 16, "abc", 3));
        struct bcdc c; assert(bcdc_parse(b, n, &c) == 0 && c.cmd == 263 && c.id == 0x1234 && (c.flags & BCDC_F_SET) && c.len == 3);
        assert(bcdc_parse(b, 10, &c) == -1);
        assert(bcdc_data_build(b) == 4 && b[0] == 0x20);
        b[3] = 2; assert(bcdc_data_skip(b, 20) == 12); assert(bcdc_data_skip(b, 8) == -1);
        b[0] = 0x10; assert(bcdc_data_skip(b, 20) == -1);
    }
    /* iovar */
    {
        uint32_t v = 0x01020304;
        size_t n = brcm_iovar(b, sizeof b, "wsec", &v, 4);
        assert(n == 9 && !memcmp(b, "wsec\0\x04\x03\x02\x01", 9));
        assert(brcm_iovar(b, 6, "wsec", &v, 4) == 0);
        assert(brcm_iovar(b, sizeof b, "ver", 0, 0) == 4);
    }
    /* escaneo */
    {
        size_t n = brcm_escan_params(b, 0x55, 0, 0);
        assert(n == 72 && b[0] == 1 && b[4] == 1 && b[6] == 0x55 && b[8] == 0 && b[8 + 36] == 0xFF && b[8 + 41] == 0xFF && b[8 + 42] == 2 && b[8 + 44] == 0xFF && b[8 + 60] == 0);
        n = brcm_escan_params(b, 1, (const uint8_t *)"casa", 4);
        assert(b[8] == 4 && !memcmp(b + 12, "casa", 4));
        assert(brcm_ssid(b, (const uint8_t *)"0123456789012345678901234567890123", 34) == 36 && b[0] == 32);
    }
    /* clave */
    {
        uint8_t k[16]; for (int i = 0; i < 16; i++) k[i] = (uint8_t)i;
        uint8_t ap[6] = { 1, 2, 3, 4, 5, 6 };
        assert(brcm_wsec_key(b, 0, k, 16, CRYPTO_ALGO_AES_CCM, 1, ap) == 164);
        assert(b[0] == 0 && b[4] == 16 && b[8] == 0 && b[23] == 15 && b[112] == 4 && b[116] == 2 && !memcmp(b + 156, ap, 6) && b[162] == 0);
        brcm_wsec_key(b, 1, k, 16, CRYPTO_ALGO_AES_CCM, 0, 0);
        assert(b[0] == 1 && b[116] == 0 && b[156] == 0);
    }
    /* CLM */
    {
        uint8_t d[5] = { 9, 8, 7, 6, 5 };
        assert(brcm_clm_chunk(b, d, 5, 1, 0) == 17 && b[0] == 2 && b[1] == 0x10 && b[2] == 2 && b[4] == 5 && b[12] == 9 && b[16] == 5);
        brcm_clm_chunk(b, d, 5, 0, 1); assert(b[0] == 4);
        brcm_clm_chunk(b, d, 5, 1, 1); assert(b[0] == 6);
    }
    /* mascara de eventos */
    {
        uint8_t m[16]; uint32_t ev[] = { 0, 16, 46, 69, 200 };
        brcm_event_mask(m, ev, 5);
        assert(m[0] == 1 && m[2] == 1 && m[5] == 0x40 && m[8] == 0x20);
    }
    /* evento: ESCAN_RESULT parcial con 3 bytes de datos */
    {
        uint8_t e[80] = { 0 };
        memset(e, 0xFF, 6); e[12] = 0x88; e[13] = 0x6C;
        uint8_t *h = e + 14; h[0] = 0x80; h[1] = 0x01; h[4] = 1; h[5] = 0x00; h[6] = 0x10; h[7] = 0x18; h[9] = 1;
        uint8_t *m = h + 10; m[7] = 69; m[11] = 8; m[23] = 3; m[24] = 0xAA; m[29] = 0xBB;
        e[72] = 1; e[73] = 2; e[74] = 3;
        struct brcm_event ev;
        assert(brcm_event_parse(e, 75, &ev) == 0 && ev.type == BRCM_E_ESCAN_RESULT && ev.status == BRCM_ESCAN_PARTIAL && ev.datalen == 3 && ev.data[2] == 3 && ev.addr[0] == 0xAA && ev.addr[5] == 0xBB);
        assert(brcm_event_parse(e, 74, &ev) == -2);
        e[13] = 0x6D; assert(brcm_event_parse(e, 75, &ev) == -1);
        assert(!strcmp(brcm_event_name(BRCM_E_LINK), "LINK"));
        for (size_t n = 0; n < sizeof e; n++) (void)brcm_event_parse(e, n, &ev);
    }
    /* DHCP */
    {
        uint8_t mac[6] = { 0xdc, 0xa6, 0x32, 1, 2, 3 }, srv[4] = { 192, 168, 1, 1 }, ip[4] = { 192, 168, 1, 42 };
        size_t n = dhcp_build(b, DHCP_DISCOVER, 0xCAFEBABE, mac, 0, 0, "soma");
        assert(n == 300 && b[0] == 1 && b[4] == 0xCA && b[7] == 0xBE && b[10] == 0x80 && !memcmp(b + 28, mac, 6) && b[236] == 99 && b[240] == 53 && b[242] == 1);
        dhcp_build(b, DHCP_REQUEST, 1, mac, ip, srv, 0);
        assert(b[242] == 3 && b[252] == 50 && !memcmp(b + 254, ip, 4) && b[258] == 54 && !memcmp(b + 260, srv, 4));
        /* respuesta del router: OFFER */
        uint8_t r[400] = { 0 };
        r[0] = 2; r[1] = 1; r[2] = 6; r[4] = 0xCA; r[5] = 0xFE; r[6] = 0xBA; r[7] = 0xBE; memcpy(r + 16, ip, 4); memcpy(r + 28, mac, 6);
        r[236] = 99; r[237] = 130; r[238] = 83; r[239] = 99;
        uint8_t opt[] = { 53, 1, 2, 0, 1, 4, 255, 255, 255, 0, 3, 4, 192, 168, 1, 1, 6, 8, 8, 8, 8, 8, 1, 1, 1, 1, 54, 4, 192, 168, 1, 1, 51, 4, 0, 0, 0x0e, 0x10, 255 };
        memcpy(r + 240, opt, sizeof opt);
        struct dhcp_lease l;
        assert(dhcp_parse(r, 240 + sizeof opt, 0xCAFEBABE, mac, &l) == DHCP_OFFER);
        assert(!memcmp(l.ip, ip, 4) && l.mask[2] == 255 && l.mask[3] == 0 && l.router[3] == 1 && l.dns[0] == 8 && l.server[0] == 192 && l.lease_s == 3600);
        assert(dhcp_parse(r, 240 + sizeof opt, 0x12345678, mac, &l) == 0);           /* otra transaccion */
        r[242] = 5; assert(dhcp_parse(r, 240 + sizeof opt, 0xCAFEBABE, mac, &l) == DHCP_ACK);
        r[242] = 6; assert(dhcp_parse(r, 240 + sizeof opt, 0xCAFEBABE, mac, &l) == DHCP_NAK);
        r[241] = 200; assert(dhcp_parse(r, 240 + sizeof opt, 0xCAFEBABE, mac, &l) == -1);
        for (size_t k = 0; k < 300; k++) (void)dhcp_parse(r, k, 0xCAFEBABE, mac, &l);
    }
    puts("test_brcm: OK");
    return 0;
}
