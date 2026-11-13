/* SOMA 024: pruebas en host del nucleo del terminal (src/apps/term_core.c): ajuste de linea, ANSI, UTF-8, desplazamiento,
 * barra, seleccion y portapapeles, con ASan/UBSan y fuzz. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "term_core.h"

/* PARCHE 106: term_core.c declara el portapapeles del sistema como simbolos DEBILES (en Linux, sin definir, valen NULL). El enlazador de
 * macOS no admite referencias debiles sin definir en un ejecutable, asi que alli se definen sin portapapeles compartido: mismo comportamiento. */
#ifdef __APPLE__
int clip_set_text(const char *t, uint32_t n) { (void)t; (void)n; return 0; }
const char *clip_text(uint32_t *n) { if (n) *n = 0; return ""; }
uint32_t clip_seq(void) { return 0; }
#endif

static struct term_core T;

static void test_filas(void)
{
    tc_init(&T, 10);
    const char *l[] = { "", "12345", "1234567890", "12345678901", "1234567890123456789012345" };
    for (int i = 0; i < 5; ++i) tc_add(&T, l[i]);
    int exp[] = { 1, 1, 1, 2, 3 };
    for (int i = 0; i < 5; ++i) CHECK_EQ_INT(tc_rows_of(&T, i), exp[i]);
    CHECK_EQ_INT(tc_total_rows(&T), 8);
    tc_set_cols(&T, 5);
    CHECK_EQ_INT(tc_total_rows(&T), 1 + 1 + 2 + 3 + 5);
    tc_set_cols(&T, 0);                                   /* se acota a 1 */
    CHECK_EQ_INT(T.cols, 1);
}

static void test_ansi_utf8(void)
{
    tc_init(&T, 80);
    tc_add(&T, "\033[1;36mhola\033[0m mundo");
    CHECK_EQ_INT(tc_vis_len(&T, 0), 10);
    struct tc_cell c[16];
    int n = tc_row_cells(&T, 0, 0, c, 16);
    CHECK_EQ_INT(n, 10);
    CHECK_EQ_INT(c[0].fg, 36); CHECK_EQ_INT(c[0].bold, 1); CHECK_EQ_INT(c[3].fg, 36);
    CHECK_EQ_INT(c[4].fg, 0); CHECK_EQ_INT(c[4].bold, 0); CHECK(c[4].b[0] == ' ');
    tc_add(&T, "a\xC3\xB1" "b");                          /* a, enie, b */
    CHECK_EQ_INT(tc_vis_len(&T, 1), 3);
    n = tc_row_cells(&T, 1, 0, c, 16);
    CHECK_EQ_INT(n, 3); CHECK_EQ_INT(c[1].nb, 2); CHECK(c[1].b[0] == (char)0xC3 && c[1].b[1] == (char)0xB1);
    tc_add(&T, "\033[31mrojo\033[");                      /* secuencia cortada: no se cuela en el texto */
    CHECK_EQ_INT(tc_vis_len(&T, 2), 4);
    tc_add(&T, "\033[99999999999m!\033[;m?");             /* parametros enormes y vacios */
    CHECK_EQ_INT(tc_vis_len(&T, 3), 2);
    tc_add(&T, "x\ty\x01z\xFF");                          /* tab y controles -> espacio; byte suelto >= 0x80 -> ? */
    n = tc_row_cells(&T, 4, 0, c, 16);
    CHECK_EQ_INT(n, 6); CHECK(c[1].b[0] == ' ' && c[3].b[0] == ' ' && c[5].b[0] == '?');
}

static void test_vista_y_scroll(void)
{
    tc_init(&T, 20);
    char b[32];
    for (int i = 0; i < 10; ++i) { snprintf(b, sizeof b, "linea %d", i); tc_add(&T, b); }
    int idx, sub;
    /* vista de 4 filas al final: lineas 6..9 */
    CHECK(tc_screen_row(&T, 4, 0, &idx, &sub) && idx == 6 && sub == 0);
    CHECK(tc_screen_row(&T, 4, 3, &idx, &sub) && idx == 9);
    CHECK(!tc_screen_row(&T, 4, 4, &idx, &sub));
    /* vista mayor que el contenido: el contenido empieza arriba y las filas de abajo estan vacias */
    CHECK(tc_screen_row(&T, 14, 0, &idx, &sub) && idx == 0);
    CHECK(tc_screen_row(&T, 14, 9, &idx, &sub) && idx == 9);
    CHECK(!tc_screen_row(&T, 14, 10, &idx, &sub));
    CHECK(!tc_screen_row(&T, 14, 13, &idx, &sub));
    tc_scroll_by(&T, 3, 4);
    CHECK_EQ_INT(T.scroll, 3);
    CHECK(tc_screen_row(&T, 4, 3, &idx, &sub) && idx == 6);
    tc_scroll_by(&T, 100, 4);
    CHECK_EQ_INT(T.scroll, 6);                            /* tope: 10 filas - 4 */
    CHECK(tc_screen_row(&T, 4, 0, &idx, &sub) && idx == 0);
    tc_scroll_by(&T, -100, 4);
    CHECK_EQ_INT(T.scroll, 0);
    /* si el usuario mira mas arriba, una linea nueva no mueve la vista */
    tc_scroll_set(&T, 5, 4);
    CHECK(tc_screen_row(&T, 4, 0, &idx, &sub)); int before = idx;
    tc_add(&T, "nueva");
    CHECK(tc_screen_row(&T, 4, 0, &idx, &sub) && idx == before);
    CHECK_EQ_INT(T.scroll, 6);
    tc_scroll_set(&T, 0, 4);
    tc_add(&T, "otra");
    CHECK_EQ_INT(T.scroll, 0);                            /* al final, sigue pegado al final */
    /* lineas ajustadas */
    tc_init(&T, 5);
    tc_add(&T, "abcdefghijkl");                           /* 3 filas */
    tc_add(&T, "xy");
    CHECK(tc_screen_row(&T, 4, 0, &idx, &sub) && idx == 0 && sub == 0);
    CHECK(tc_screen_row(&T, 4, 2, &idx, &sub) && idx == 0 && sub == 2);
    CHECK(tc_screen_row(&T, 4, 3, &idx, &sub) && idx == 1 && sub == 0);
    struct tc_cell c[8];
    CHECK_EQ_INT(tc_row_cells(&T, 0, 2, c, 8), 2); CHECK(c[0].b[0] == 'k' && c[1].b[0] == 'l');
    tc_clear(&T);
    CHECK_EQ_INT(T.count, 0); CHECK_EQ_INT(tc_total_rows(&T), 0);
}

static void test_anillo(void)
{
    tc_init(&T, 80);
    char b[32];
    for (int i = 0; i < 1500; ++i) { snprintf(b, sizeof b, "l%d", i); tc_add(&T, b); }
    CHECK_EQ_INT(T.count, TC_LINES);
    struct tc_cell c[8];
    tc_row_cells(&T, 0, 0, c, 8); CHECK(c[0].b[0] == 'l' && c[1].b[0] == '5' && c[2].b[0] == '0' && c[3].b[0] == '0');   /* l500 es la mas antigua */
    tc_row_cells(&T, TC_LINES - 1, 0, c, 8); CHECK(c[1].b[0] == '1' && c[2].b[0] == '4' && c[3].b[0] == '9' && c[4].b[0] == '9');
    /* con la vista desplazada, descartar lineas viejas no deja el scroll fuera de rango */
    tc_scroll_set(&T, tc_scroll_max(&T, 10), 10);
    for (int i = 0; i < 50; ++i) tc_add(&T, "mas");
    CHECK(T.scroll >= 0 && T.scroll <= tc_scroll_max(&T, 10));
    /* linea larga: se trunca sin desbordar */
    char big[1000]; memset(big, 'z', 999); big[999] = 0;
    tc_add(&T, big);
    CHECK(tc_vis_len(&T, T.count - 1) <= TC_RAW - 1);
}

static void test_viva(void)
{
    tc_init(&T, 10);
    tc_add(&T, "uno"); tc_add(&T, "soma> ");              /* la ultima es la linea del prompt */
    CHECK_EQ_INT(T.count, 2);
    tc_replace_last(&T, "soma> echo hola mundo");          /* 21 car -> 3 filas */
    CHECK_EQ_INT(tc_vis_len(&T, 1), 21); CHECK_EQ_INT(tc_rows_of(&T, 1), 3);
    tc_pop(&T); CHECK_EQ_INT(T.count, 1);
    tc_add(&T, "dos"); tc_add(&T, "soma> ");
    CHECK_EQ_INT(T.count, 3);
    uint32_t id = T.next_id; tc_pop(&T); tc_add(&T, "x"); CHECK_EQ_INT(T.next_id, id);   /* los ids no se disparan */
    tc_pop(&T); tc_pop(&T); tc_pop(&T); tc_pop(&T);        /* vaciar y pasarse es inocuo */
    CHECK_EQ_INT(T.count, 0);
    tc_replace_last(&T, "desde vacio"); CHECK_EQ_INT(T.count, 1);
    /* si se mira mas arriba y el prompt crece por ajuste, la vista no salta */
    tc_init(&T, 10);
    for (int i = 0; i < 30; ++i) tc_add(&T, "relleno");
    tc_add(&T, "$ ");
    tc_scroll_set(&T, 7, 5);
    int idx, sub; CHECK(tc_screen_row(&T, 5, 0, &idx, &sub)); int top = idx;
    tc_replace_last(&T, "$ comando largo que ocupa varias filas");
    CHECK(tc_screen_row(&T, 5, 0, &idx, &sub) && idx == top);
}

static void test_barra(void)
{
    tc_init(&T, 80);
    int ty, th;
    for (int i = 0; i < 5; ++i) tc_add(&T, "x");
    CHECK(!tc_thumb(&T, 10, 200, 20, &ty, &th));          /* cabe todo: sin barra */
    for (int i = 0; i < 95; ++i) tc_add(&T, "x");         /* 100 filas, vista de 10 */
    CHECK(tc_thumb(&T, 10, 200, 20, &ty, &th));
    CHECK_EQ_INT(th, 20); CHECK_EQ_INT(ty, 180);          /* al final: pulgar abajo */
    tc_scroll_set(&T, 90, 10);
    tc_thumb(&T, 10, 200, 20, &ty, &th); CHECK_EQ_INT(ty, 0);
    tc_scroll_set(&T, 45, 10);
    tc_thumb(&T, 10, 200, 20, &ty, &th); CHECK(ty > 80 && ty < 100);
    /* ida y vuelta: el pulgar en y devuelve un scroll consistente (monotono, en rango) */
    int prev = 1 << 30;
    for (int y = 0; y <= 180; y += 6) {
        int s = tc_scroll_from_thumb(&T, 10, 200, 20, y);
        CHECK(s >= 0 && s <= 90); CHECK(s <= prev); prev = s;
    }
    CHECK_EQ_INT(tc_scroll_from_thumb(&T, 10, 200, 20, 0), 90);
    CHECK_EQ_INT(tc_scroll_from_thumb(&T, 10, 200, 20, 180), 0);
    CHECK_EQ_INT(tc_scroll_from_thumb(&T, 10, 200, 20, 9999), 0);     /* fuera de la pista: se acota */
    CHECK_EQ_INT(tc_scroll_from_thumb(&T, 10, 200, 20, -50), 90);
    /* mucho contenido: el pulgar no baja del minimo */
    for (int i = 0; i < 900; ++i) tc_add(&T, "x");
    tc_thumb(&T, 10, 200, 24, &ty, &th); CHECK_EQ_INT(th, 24);
}

static void sel_text(char *out, int max) { tc_sel_text(&T, out, max); }

static void test_seleccion(void)
{
    char out[512];
    tc_init(&T, 10);
    tc_add(&T, "\033[1;36msoma\033[0m> ls /big");           /* "soma> ls /big" = 13 caracteres (2 filas de 10) */
    tc_add(&T, "segunda linea");
    tc_add(&T, "tercera");
    int idx, vcol;
    /* una linea, parcial: columnas 0..3 -> "soma" (los codigos ANSI no se copian) */
    tc_sel_begin(&T, 0, 0); tc_sel_extend(&T, 0, 3);
    sel_text(out, sizeof out); CHECK_STR(out, "soma");
    CHECK(tc_is_selected(&T, 0, 0) && tc_is_selected(&T, 0, 3) && !tc_is_selected(&T, 0, 4));
    /* al reves (de derecha a izquierda): mismo resultado */
    tc_sel_begin(&T, 0, 3); tc_sel_extend(&T, 0, 0);
    sel_text(out, sizeof out); CHECK_STR(out, "soma");
    /* una linea ajustada en dos filas: NO hay salto de linea dentro (es una sola linea logica) */
    tc_sel_begin(&T, 0, 6); tc_sel_extend(&T, 0, 12);
    sel_text(out, sizeof out); CHECK_STR(out, "ls /big");
    /* varias lineas */
    tc_sel_begin(&T, 0, 6); tc_sel_extend(&T, 2, 2);
    sel_text(out, sizeof out); CHECK_STR(out, "ls /big\nsegunda linea\nter");
    /* seleccion sobre la pantalla: fila 1 col 3 -> linea 0, columna 13 (clamp al final) */
    CHECK(tc_hit(&T, 5, 3, 7, &idx, &vcol));
    /* vista de 5 filas: total 2+2+1=5 filas -> fila 0 = linea 0 sub 0 */
    CHECK(tc_hit(&T, 5, 1, 7, &idx, &vcol)); CHECK_EQ_INT(idx, 0); CHECK_EQ_INT(vcol, 13);   /* 10 + 7 -> clamp a 13 */
    CHECK(tc_hit(&T, 5, 0, 4, &idx, &vcol)); CHECK_EQ_INT(idx, 0); CHECK_EQ_INT(vcol, 4);
    CHECK(!tc_hit(&T, 8, 7, 0, &idx, &vcol));             /* fila vacia por debajo del contenido */
    /* palabra: "/big" con rutas, y espacios */
    tc_select_word(&T, 0, 9);                             /* col 9 = '/' */
    sel_text(out, sizeof out); CHECK_STR(out, "/big");
    tc_select_word(&T, 0, 5);                             /* el espacio tras "soma>": la tanda de separadores */
    sel_text(out, sizeof out); CHECK_STR(out, "> ");
    tc_select_word(&T, 1, 0);
    sel_text(out, sizeof out); CHECK_STR(out, "segunda");
    /* linea y todo */
    tc_select_line(&T, 1); sel_text(out, sizeof out); CHECK_STR(out, "segunda linea");
    tc_select_all(&T); sel_text(out, sizeof out); CHECK_STR(out, "soma> ls /big\nsegunda linea\ntercera");
    /* acotado por el tamano de salida */
    char small[8]; int n = tc_sel_text(&T, small, sizeof small);
    CHECK(n <= 7 && strlen(small) == (size_t)n);
    tc_sel_clear(&T); sel_text(out, sizeof out); CHECK_STR(out, "");
    CHECK(!tc_is_selected(&T, 0, 0));
    /* UTF-8 se copia entero */
    tc_add(&T, "ni\xC3\xB1o");
    tc_select_line(&T, 3); sel_text(out, sizeof out); CHECK_STR(out, "ni\xC3\xB1o");
    /* el anillo descarta lineas seleccionadas: sin colgarse ni desbordar */
    tc_init(&T, 80);
    for (int i = 0; i < 10; ++i) tc_add(&T, "abc");
    tc_select_all(&T);
    for (int i = 0; i < 2000; ++i) tc_add(&T, "zzz");
    n = tc_sel_text(&T, out, sizeof out); CHECK(n >= 0 && n < (int)sizeof out);
}

static void test_bordes(void)
{
    char out[256];
    /* sin espacios sobrantes al final de cada linea (salvo la ultima seleccionada) */
    tc_init(&T, 40);
    tc_add(&T, "abc   "); tc_add(&T, "next");
    tc_select_all(&T); tc_sel_text(&T, out, sizeof out); CHECK_STR(out, "abc\nnext");
    /* extremos de la seleccion: la celda anterior al inicio y la posterior al final NO estan seleccionadas */
    tc_init(&T, 40);
    tc_add(&T, "0123456789");
    tc_sel_begin(&T, 0, 2); tc_sel_extend(&T, 0, 5);
    CHECK(!tc_is_selected(&T, 0, 1)); CHECK(tc_is_selected(&T, 0, 2)); CHECK(tc_is_selected(&T, 0, 5)); CHECK(!tc_is_selected(&T, 0, 6));
    tc_sel_text(&T, out, sizeof out); CHECK_STR(out, "2345");
    /* signos de apertura (C2 BF / C2 A1) cuentan como un caracter */
    tc_init(&T, 40);
    tc_add(&T, "\xC2\xBFqu\xC3\xA9?\xC2\xA1" "no!");
    CHECK_EQ_INT(tc_vis_len(&T, 0), 9);
    tc_select_all(&T); tc_sel_text(&T, out, sizeof out); CHECK_STR(out, "\xC2\xBFqu\xC3\xA9?\xC2\xA1" "no!");
    /* al ensanchar, el desplazamiento no se queda por encima del contenido */
    tc_init(&T, 5);
    for (int i = 0; i < 40; ++i) tc_add(&T, "abcdefghijklmnopqrst");     /* 4 filas cada una */
    tc_scroll_set(&T, tc_scroll_max(&T, 10), 10);
    tc_set_cols(&T, 200);
    CHECK(T.scroll <= tc_total_rows(&T));
    CHECK_EQ_INT(tc_total_rows(&T), 40);
}

static void test_portapapeles(void)
{
    int n; tc_clip_set("hola", 4);
    const char *s = tc_clip_get(&n); CHECK_EQ_INT(n, 4); CHECK_STR(s, "hola");
    char big[TC_CLIP_MAX + 100]; memset(big, 'q', sizeof big);
    tc_clip_set(big, sizeof big); tc_clip_get(&n); CHECK_EQ_INT(n, TC_CLIP_MAX);
    tc_clip_set("", 0); s = tc_clip_get(&n); CHECK_EQ_INT(n, 0); CHECK_STR(s, "");
}

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }

static void test_fuzz(void)
{
    tc_init(&T, 40);
    char buf[300], out[4096];
    for (int it = 0; it < 60000; ++it) {
        switch (rnd() % 9) {
        case 0: case 1: case 2: {
            int n = (int)(rnd() % 280);
            for (int i = 0; i < n; ++i) { uint32_t r = rnd() % 10; buf[i] = r < 2 ? 27 : r < 3 ? '[' : r < 4 ? ';' : r < 5 ? 'm' : r < 6 ? (char)('0' + rnd() % 10) : r < 7 ? (char)(0xC0 + rnd() % 64) : (char)(32 + rnd() % 95); }
            buf[n] = 0; tc_add(&T, buf); break; }
        case 3: tc_set_cols(&T, (int)(rnd() % 100) - 5); break;
        case 4: tc_scroll_by(&T, (int)(rnd() % 61) - 30, 1 + (int)(rnd() % 40)); break;
        case 5: if (T.count) { int i = (int)(rnd() % T.count); tc_sel_begin(&T, i, (int)(rnd() % 300) - 20); i = (int)(rnd() % T.count); tc_sel_extend(&T, i, (int)(rnd() % 300) - 20); } break;
        case 6: if (T.count) tc_select_word(&T, (int)(rnd() % T.count), (int)(rnd() % 300) - 10); break;
        case 7: { int n = tc_sel_text(&T, out, 1 + (int)(rnd() % 4000)); CHECK(n >= 0 && n < 4096); break; }
        case 8: { int vr = 1 + (int)(rnd() % 50), idx, sub, ty, th;
                  for (int r = 0; r < vr; ++r) if (tc_screen_row(&T, vr, r, &idx, &sub)) { struct tc_cell c[64]; CHECK(idx >= 0 && idx < T.count && sub >= 0 && sub < tc_rows_of(&T, idx)); tc_row_cells(&T, idx, sub, c, 64); tc_is_selected(&T, idx, sub); }
                  if (tc_thumb(&T, vr, 100 + (int)(rnd() % 300), 20, &ty, &th)) { CHECK(ty >= 0 && ty + th <= 400 && th >= 1); tc_scroll_from_thumb(&T, vr, 200, 20, (int)(rnd() % 300) - 50); }
                  if (rnd() % 50 == 0) tc_clear(&T);
                  break; }
        }
        CHECK(T.scroll >= 0 && T.count >= 0 && T.count <= TC_LINES);
    }
}

int main(void)
{
    test_filas(); test_ansi_utf8(); test_vista_y_scroll(); test_anillo(); test_viva(); test_barra(); test_seleccion(); test_bordes(); test_portapapeles(); test_fuzz();
    TEST_MAIN_END("term_core");
}
