/* PARCHE 079: pruebas de la parte pura del Wi-Fi (SDIO + chip Broadcom). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/sdio_proto.h"

int main(void)
{
    /* CMD52: leer CCCR 0x00 de la funcion 0; escribir 0x02 en IOEN (0x02) con lectura posterior */
    assert(sdio_cmd52_arg(0, 0, 0, 0, 0) == 0);
    assert(sdio_cmd52_arg(1, 0, 0x02, 0x02, 1) == (0x80000000u | 0x08000000u | (0x02u << 9) | 0x02u));
    assert(sdio_cmd52_arg(0, 1, SB_F1_CHIPCLKCSR, 0, 0) == ((1u << 28) | (0x1000Eu << 9)));
    /* CMD53: 4 bytes de la funcion 1 con direccion incremental; 512 se codifica como 0 */
    assert(sdio_cmd53_arg(0, 1, 0, 1, 0x8000, 4) == ((1u << 28) | (1u << 26) | (0x8000u << 9) | 4u));
    assert((sdio_cmd53_arg(1, 2, 1, 0, 0, 512) & 0x1FFu) == 0 && (sdio_cmd53_arg(1, 2, 1, 0, 0, 512) >> 31));
    assert(sdio_cmd53_arg(0, 1, 0, 1, 0, 0) == 0 && sdio_cmd53_arg(0, 1, 0, 1, 0, 513) == 0 && sdio_cmd53_arg(0, 1, 0, 1, 0x20000, 4) == 0);
    /* R5 */
    assert(sdio_r5_ok(0x1000 | 0xAB) && !sdio_r5_ok(0x8000) && !sdio_r5_ok(0x0800) && !sdio_r5_ok(0x0100) && sdio_r5_ok(0x2000));
    /* R4 */
    assert(SDIO_R4_READY(0xA0FF8000u) && SDIO_R4_NFUNC(0xA0FF8000u) == 2 && SDIO_R4_OCR(0xA0FF8000u) == 0xFF8000u);
    /* CIS: FUNCID, nula, MANFID (Broadcom 0x02d0, tarjeta 0xa9a6), fin */
    {
        uint8_t cis[] = { 0x21, 0x02, 0x0C, 0x00, 0x00, 0x20, 0x04, 0xD0, 0x02, 0xA6, 0xA9, 0xFF };
        uint16_t m = 0, c = 0;
        assert(sdio_cis_manfid(cis, sizeof cis, &m, &c) == 0 && m == 0x02D0 && c == 0xA9A6);
        uint8_t bad[] = { 0x21, 0x40, 0x00 };
        assert(sdio_cis_manfid(bad, sizeof bad, &m, &c) == -1);
        uint8_t none[] = { 0x21, 0x02, 0x0C, 0x00, 0xFF };
        assert(sdio_cis_manfid(none, sizeof none, &m, &c) == -1);
        uint8_t shortm[] = { 0x20, 0x04, 0xD0, 0x02 };
        assert(sdio_cis_manfid(shortm, sizeof shortm, &m, &c) == -1);
    }
    /* ventana */
    {
        uint8_t w[3];
        sb_window(0x18000000u, w); assert(w[0] == 0 && w[1] == 0 && w[2] == 0x18);
        sb_window(0x18108000u, w); assert(w[0] == 0x80 && w[1] == 0x10 && w[2] == 0x18);
        sb_window(0x00198000u, w); assert(w[0] == 0x80 && w[1] == 0x19 && w[2] == 0);
    }
    /* chip id: 43455 rev 6, AI */
    {
        struct brcm_chipid c;
        brcm_chipid_decode(0x15264345u, &c);
        assert(c.id == 0x4345 && c.rev == 6 && c.pkg == 2 && c.ncores == 5 && c.socitype == 1);
    }
    /* EROM sintetica: chipcommon (1 MP, esclavo + envoltorio maestro), nucleo sin envoltorio (se omite), ARM CR4 (envoltorio esclavo,
     * descriptor de tamano de 2 palabras), hueco y fin */
    {
        uint32_t e[] = {
            0x4BF00001u | (0x800u << 8), (0x2Bu << 24) | (1u << 14) | (1u << 4) | 1u,
            0x00000003u,                                       /* MP */
            0x18000005u,                                       /* esclavo puerto 0, 4K */
            0x18100000u | 0xC0u | 0x5u,                        /* envoltorio maestro */
            0x4BF00001u | (0x367u << 8), (0x0u << 24) | 1u,    /* sin envoltorios */
            0x18109005u,
            0x00000000u,                                       /* hueco */
            0x4BF00001u | (0x83Eu << 8), (0x07u << 24) | (1u << 19) | 1u,
            0x18002005u | 0x30u, 0x00001008u, 0x0u,            /* esclavo con tamano SG32 (2 palabras) */
            0x18102000u | 0x80u | 0x5u,                        /* envoltorio esclavo */
            0x0000000Fu
        };
        struct brcm_core c[8];
        int n = brcm_erom_parse(e, sizeof e / 4, c, 8);
        assert(n == 2);
        assert(c[0].id == 0x800 && c[0].rev == 0x2B && c[0].base == 0x18000000u && c[0].wrap == 0x18100000u);
        assert(c[1].id == 0x83E && c[1].rev == 7 && c[1].base == 0x18002000u && c[1].wrap == 0x18102000u);
        assert(brcm_erom_parse(e, 6, c, 8) == -1);                 /* cortada: sin fin */
        assert(brcm_erom_parse(e, sizeof e / 4, c, 1) == 2);      /* cuenta aunque no quepan */
        assert(!strcmp(brcm_core_name(CORE_ARM_CR4), "ARM CR4"));
    }
    /* RAM del CR4 */
    assert(brcm_cr4_bank_bytes(0x0F) == 16u * 8192u && brcm_cr4_bank_bytes(0x200 | 0x1F) == 32u * 1024u);
    assert(brcm_rambase(0x4345) == 0x198000u && brcm_rambase(0x1234) == 0);
    /* NVRAM */
    {
        const char *t = "# comentario\r\nNVRAMRev=$Rev: 498373 $\r\n  sromrev=11\n\nvendid=0x14e4 \n#macaddr=00:90\nsin_igual\n=vacio\nxtalfreq=37400";
        uint8_t o[256];
        int n = brcm_nvram_pack(t, (uint32_t)strlen(t), o, sizeof o);
        const char exp[] = "NVRAMRev=$Rev: 498373 $\0sromrev=11\0vendid=0x14e4\0xtalfreq=37400\0";
        uint32_t body = sizeof exp;                         /* incluye el NUL final de la lista */
        uint32_t pad = (body + 3) & ~3u;
        assert(n == (int)pad + 4);
        assert(!memcmp(o, exp, sizeof exp));
        for (uint32_t i = body; i < pad; i++) assert(o[i] == 0);
        uint32_t tok = (uint32_t)o[pad] | (uint32_t)o[pad + 1] << 8 | (uint32_t)o[pad + 2] << 16 | (uint32_t)o[pad + 3] << 24;
        assert((tok & 0xFFFF) == pad / 4 && (tok >> 16) == (~(pad / 4) & 0xFFFF));
        assert(brcm_is_nvram_token(tok) && !brcm_is_nvram_token(0x0019F3A0u));
        assert(brcm_nvram_pack(t, (uint32_t)strlen(t), o, 20) == -1);
        assert(brcm_nvram_pack("", 0, o, sizeof o) == 8);
    }
    /* estructura compartida */
    {
        uint8_t b[32] = { 0x03, 0x04, 0x00, 0x00 };
        b[20] = 0x10; b[21] = 0x20; b[22] = 0x1A;           /* consola en 0x1A2010 */
        struct brcm_shared s;
        assert(brcm_shared_parse(b, 32, &s) == 0 && (s.flags & SHARED_TRAP) && s.console_addr == 0x1A2010u);
        b[0] = 0x09; assert(brcm_shared_parse(b, 32, &s) == -2);
        assert(brcm_shared_parse(b, 20, &s) == -1);
    }
    /* placa: Pi 5 de 8 GB (d04170), Pi 4 de 4 GB (c03111), antigua (000e) */
    {
        struct rpi_board p;
        rpi_board_decode(0xD04170u, &p); assert(p.new_style && p.type == 0x17 && p.mem_mb == 8192 && p.has_wifi && !strcmp(p.name, "Raspberry Pi 5"));
        rpi_board_decode(0xC03111u, &p); assert(p.type == 0x11 && p.mem_mb == 4096 && p.rev == 1);
        rpi_board_decode(0xB04170u, &p); assert(p.mem_mb == 2048);
        rpi_board_decode(0x000Eu, &p); assert(!p.new_style && !p.has_wifi);
    }
    puts("test_sdio: OK");
    return 0;
}
