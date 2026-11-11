#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/mbox_proto.h"
/* Simula al firmware: marca como atendidas las etiquetas y rellena las respuestas. */
static void firmware(uint32_t *b, uint32_t base, uint32_t size, uint32_t pitch, uint32_t w, uint32_t h)
{
    b[1] = 0x80000000u;
    for (uint32_t i = 2; b[i];) {
        uint32_t id = b[i], n = b[i + 1] / 4;
        b[i + 2] = 0x80000000u | (n * 4);
        if (id == 0x00040001u) { b[i + 3] = base; b[i + 4] = size; }
        if (id == 0x00040008u) b[i + 3] = pitch;
        if (id == 0x00048003u) { b[i + 3] = w; b[i + 4] = h; }
        i += 3 + n;
    }
}
int main(void)
{
    uint32_t b[MBOX_FB_WORDS]; struct mbox_fb_result r;
    assert(mbox_fb_build(b, MBOX_FB_WORDS, 1024, 768, 0) == MBOX_FB_WORDS && b[0] == MBOX_FB_WORDS * 4 && b[1] == 0);
    assert(b[2] == 0x48003 && b[3] == 8 && b[5] == 1024 && b[6] == 768);
    assert(mbox_fb_build(b, 10, 1024, 768, 0) == 0 && mbox_fb_build(b, MBOX_FB_WORDS, 0, 768, 0) == 0 && mbox_fb_build(b, MBOX_FB_WORDS, 9000, 1, 0) == 0);
    mbox_fb_build(b, MBOX_FB_WORDS, 1024, 768, 1);
    assert(mbox_fb_parse(b, MBOX_FB_WORDS, &r) == -1);                       /* sin atender */
    firmware(b, 0x3FD00000u, 3u << 20, 4096, 1024, 768);
    /* la profundidad solo la devuelve el firmware: la peticion ya la lleva */
    assert(mbox_fb_parse(b, MBOX_FB_WORDS, &r) == 0 && r.base == 0x3FD00000u && r.pitch == 4096 && r.width == 1024 && r.height == 768 && r.depth == 32);
    firmware(b, 0x3FD00000u, 1u << 20, 4096, 1024, 768);                     /* 3 MiB necesarios, 1 MiB dado */
    assert(mbox_fb_parse(b, MBOX_FB_WORDS, &r) == -4);
    firmware(b, 0, 3u << 20, 4096, 1024, 768);
    assert(mbox_fb_parse(b, MBOX_FB_WORDS, &r) == -4);
    firmware(b, 0x3FD00000u, 3u << 20, 100, 1024, 768);
    assert(mbox_fb_parse(b, MBOX_FB_WORDS, &r) == -4);
    firmware(b, 0x3FD00000u, 3u << 20, 4096, 1024, 768);
    b[8] = 0x7fffffff; assert(mbox_fb_parse(b, MBOX_FB_WORDS, &r) < 0);       /* tamano de etiqueta absurdo */
    uint32_t c[MBOX_FB_WORDS];
    srand(7);
    for (int k = 0; k < 200000; k++) {                                       /* basura: no debe salirse del buffer */
        for (int j = 0; j < MBOX_FB_WORDS; j++) c[j] = (uint32_t)rand() * 2654435761u;
        if (k & 1) { c[0] = (MBOX_FB_WORDS * 4) - (uint32_t)(rand() & 3) * 4; c[1] = 0x80000000u; }
        if (k & 2) c[2 + (rand() % 10)] = 0x80000000u | (uint32_t)(rand() % 20);
        (void)mbox_fb_parse(c, MBOX_FB_WORDS, &r);
        (void)mbox_fb_parse_any(c, MBOX_FB_WORDS, &r);
    }
    {   /* PARCHE 075: respuesta como la de una Pi 5 real (tamano impuesto, etiquetas sin atender) */
        uint32_t q[MBOX_FB_WORDS]; struct mbox_fb_result a;
        assert(mbox_fb_build_query(q, MBOX_FB_WORDS) == MBOX_FB_WORDS && mbox_fb_build_query(q, 8) == 0);
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == -1);                       /* sin atender */
        q[1] = 0x80000000u;
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == -4);                       /* atendido pero sin datos */
        /* GETPHYS en q[2..6], GETDEPTH q[7..10], ALLOC q[11..15], PITCH q[16..19] */
        q[4] = 0x80000008u; q[5] = 1920; q[6] = 1080;
        q[13] = 0x80000008u; q[14] = 0x3F000000u; q[15] = 1920 * 1080 * 4;
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.width == 1920 && a.height == 1080 && a.depth == 32 && a.pitch == 7680 && a.base == 0x3F000000u);
        q[9] = 0x80000004u; q[10] = 16; q[18] = 0x80000004u; q[19] = 3840;
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.depth == 16 && a.pitch == 3840);
        q[3] = 5;                                                                    /* longitud mal formada */
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == -2);
        mbox_fb_build(q, MBOX_FB_WORDS, 1280, 720, 0); q[1] = 0x80000000u;          /* peticion completa: el firmware impone 1920x1080 y no atiende el resto */
        q[4] = 0x80000008u; q[5] = 1920; q[6] = 1080;
        for (uint32_t k = 2; k < MBOX_FB_WORDS - 4; k++) if (q[k] == 0x00040001u) { q[k + 2] = 0x80000008u; q[k + 3] = 0xC1000000u; q[k + 4] = 8294400; break; }
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.width == 1920 && a.base == 0xC1000000u && a.pitch == 7680);
        assert(mbox_fb_parse(q, MBOX_FB_WORDS, &a) != 0);                            /* la estricta la rechaza */
        /* PARCHE 076: sin etiqueta de profundidad: se deduce de la pitch o del tamano */
        mbox_fb_build_query(q, MBOX_FB_WORDS); q[1] = 0x80000000u;
        q[4] = 0x80000008u; q[5] = 1920; q[6] = 1080;
        q[13] = 0x80000008u; q[14] = 0xC1000000u; q[15] = 1920 * 1080 * 2;
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.depth == 16 && a.pitch == 3840);    /* solo tamano: 2 bytes por pixel */
        q[18] = 0x80000004u; q[19] = 3840;
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.depth == 16 && a.pitch == 3840);    /* con pitch */
        q[19] = 5760;
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.depth == 24);
        q[19] = 7680;
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.depth == 32 && a.pitch == 7680);
    }
    {   /* PARCHE 077: la Pi 5 real declara depth 32 con pitch de 16 bits (firmware#1904): manda la pitch */
        uint32_t q[MBOX_FB_WORDS]; struct mbox_fb_result a;
        mbox_fb_build(q, MBOX_FB_WORDS, 1920, 1080, 0); q[1] = 0x80000000u;
        for (uint32_t k = 2; k + 4 < MBOX_FB_WORDS; ) {
            uint32_t id = q[k], n = q[k + 1] / 4;
            if (!id) break;
            q[k + 2] = 0x80000000u | q[k + 1];
            if (id == 0x00040001u) { q[k + 3] = 0x3F800000u; q[k + 4] = 4147200; }
            if (id == 0x00040008u) q[k + 3] = 0xF00;
            k += 3 + n;
        }
        assert(mbox_fb_parse(q, MBOX_FB_WORDS, &a) != 0);                            /* la estricta no la acepta (pitch < ancho*4) */
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.width == 1920 && a.height == 1080 && a.depth == 16 && a.pitch == 3840 && a.size == 4147200);
        for (uint32_t k = 2; k + 4 < MBOX_FB_WORDS; k++) if (q[k] == 0x00040008u) { q[k + 2] = 0; break; }   /* sin pitch: decide el tamano */
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.depth == 16 && a.pitch == 3840);
        for (uint32_t k = 2; k + 4 < MBOX_FB_WORDS; k++) if (q[k] == 0x00040001u) { q[k + 4] = 1920 * 1080 * 4; break; }
        assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.depth == 32 && a.pitch == 7680);
    }
    {   /* PARCHE 078: datos de la placa real: monitor 1920x1080 a 16 bits (pitch 3840, tamano 4147200); el firmware devuelve el tamano pedido y depth 32 */
        struct mbox_fb_result a = { 0x3F800000u, 4147200, 3840, 800, 600, 32 };           /* eco de 800x600: pasaba la comprobacion estricta */
        assert(!mbox_fb_consistent(&a));
        assert(mbox_fb_fix(&a, 0, 0) == 1 && a.width == 1920 && a.height == 1080 && a.depth == 16 && a.pitch == 3840);
        assert(mbox_fb_consistent(&a) && mbox_fb_fix(&a, 0, 0) == 0);
        struct mbox_fb_result b = { 0x3F800000u, 4147200, 3840, 1280, 720, 24 };             /* eco de 1280x720: 3840/1280 = 3 parecia 24 bits */
        assert(mbox_fb_fix(&b, 1920, 1080) == 1 && b.width == 1920 && b.height == 1080 && b.depth == 16);
        struct mbox_fb_result c = { 0x3F800000u, 4147200, 3840, 1920, 1080, 32 };            /* eco de 1920x1080 con depth 32 */
        assert(mbox_fb_fix(&c, 0, 0) == 1 && c.width == 1920 && c.height == 1080 && c.depth == 16);
        struct mbox_fb_result d = { 0x3FD00000u, 3145728, 4096, 1024, 768, 32 };             /* emulador: modo real de 32 bits, no se toca */
        assert(mbox_fb_consistent(&d) && mbox_fb_fix(&d, 1024, 768) == 0 && d.width == 1024 && d.depth == 32);
        struct mbox_fb_result e = { 0x3F800000u, 8294400, 7680, 800, 600, 32 };              /* pantalla real de 32 bits (framebuffer_depth=32) con eco */
        assert(mbox_fb_fix(&e, 1920, 1080) == 1 && e.width == 1920 && e.height == 1080 && e.depth == 32);
        struct mbox_fb_result f = { 0x3F800000u, 2560u * 1440u * 2u, 5120, 800, 600, 32 };   /* monitor 2560x1440 a 16 bits, sin tamano fisico */
        assert(mbox_fb_fix(&f, 0, 0) == 1 && f.width == 2560 && f.height == 1440 && f.depth == 16);
        struct mbox_fb_result g = { 0x3F800000u, 0, 3840, 800, 600, 32 };                     /* sin tamano: no se inventa nada */
        assert(mbox_fb_fix(&g, 0, 0) == 0 && g.width == 800);
        struct mbox_fb_result h = { 0x3F800000u, 4147200, 3840, 800, 600, 32 };               /* tamano fisico que no cuadra con la pitch: se ignora */
        assert(mbox_fb_fix(&h, 1000, 700) == 1 && h.width == 1920 && h.height == 1080 && h.depth == 16);
        uint32_t q[16]; uint32_t pw = 0, ph = 0;
        assert(mbox_fb_build_getphys(q, 16) == 8 && mbox_fb_build_getphys(q, 4) == 0 && mbox_fb_parse_phys(q, 8, &pw, &ph) == -1);
        q[1] = 0x80000000u; q[4] = 0x80000008u; q[5] = 1920; q[6] = 1080;
        assert(mbox_fb_parse_phys(q, 8, &pw, &ph) == 0 && pw == 1920 && ph == 1080);
        q[5] = 0; assert(mbox_fb_parse_phys(q, 8, &pw, &ph) == -1);
        for (int k = 0; k < 20000; k++) {                                                     /* entradas arbitrarias: sin division por cero ni desbordes */
            struct mbox_fb_result z = { (uint32_t)rand(), (uint32_t)rand() * 3u, (uint32_t)rand() % 9000u, (uint32_t)rand() % 9000u, (uint32_t)rand() % 9000u, (uint32_t)(rand() % 5) * 8u };
            (void)mbox_fb_fix(&z, (uint32_t)rand() % 5000u, (uint32_t)rand() % 5000u);
            if (z.pitch && z.size >= z.pitch && z.width && z.depth) assert(z.depth == 16 || z.depth == 24 || z.depth == 32 || z.depth == 0 || z.depth == 8);
        }
    }
    {   /* PARCHE 078: de punta a punta, con la respuesta de la placa a las peticiones de 800x600, 1280x720 y 1920x1080 */
        static const uint32_t req[3][2] = { { 800, 600 }, { 1280, 720 }, { 1920, 1080 } };
        for (int t = 0; t < 3; t++) {
            uint32_t q[MBOX_FB_WORDS]; struct mbox_fb_result a;
            mbox_fb_build(q, MBOX_FB_WORDS, req[t][0], req[t][1], 0); q[1] = 0x80000000u;
            for (uint32_t k = 2; k + 4 < MBOX_FB_WORDS; ) {
                uint32_t id = q[k], n = q[k + 1] / 4;
                if (!id) break;
                q[k + 2] = 0x80000000u | q[k + 1];                                   /* todo "aceptado": eco de lo pedido */
                if (id == 0x00040001u) { q[k + 3] = 0x3F800000u; q[k + 4] = 4147200; }
                if (id == 0x00040008u) q[k + 3] = 3840;
                k += 3 + n;
            }
            int strict = mbox_fb_parse(q, MBOX_FB_WORDS, &a);
            if (t == 0) assert(strict == 0 && a.width == 800 && !mbox_fb_consistent(&a));   /* el caso que enganaba a los parches 075-077 */
            assert(mbox_fb_parse_any(q, MBOX_FB_WORDS, &a) == 0 && a.pitch == 3840 && a.size == 4147200);
            (void)mbox_fb_fix(&a, 0, 0);
            assert(a.width == 1920 && a.height == 1080 && a.depth == 16 && a.pitch == 3840 && mbox_fb_consistent(&a));
        }
    }
    puts("mbox_proto: todo correcto");
    return 0;
}
