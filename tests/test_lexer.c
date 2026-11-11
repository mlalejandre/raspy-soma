/* Pruebas en host del lexer de SOMA (src/lang/lexer.c). */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "lexer.h"

static struct soma_token next_of(const char *src, struct soma_lexer *lex, int fresh)
{
    if (fresh) lexer_init(lex, src, (uint32_t)strlen(src));
    return lexer_next(lex);
}

static void test_basico(void)
{
    struct soma_lexer lx;
    struct soma_token t;

    t = next_of("let x = 42;", &lx, 1);
    CHECK_EQ_INT(t.type, TOK_LET);
    t = next_of(0, &lx, 0);
    CHECK_EQ_INT(t.type, TOK_IDENT);
    CHECK_STR(t.text, "x");
    t = next_of(0, &lx, 0);
    CHECK_EQ_INT(t.type, TOK_ASSIGN);
    t = next_of(0, &lx, 0);
    CHECK_EQ_INT(t.type, TOK_INT);
    CHECK_EQ_INT(t.int_val, 42);
    t = next_of(0, &lx, 0);
    CHECK_EQ_INT(t.type, TOK_SEMICOLON);
    t = next_of(0, &lx, 0);
    CHECK_EQ_INT(t.type, TOK_EOF);
}

static void test_palabras_clave_y_operadores(void)
{
    struct soma_lexer lx;
    struct soma_token t;

    t = next_of("if else while for fn return true false", &lx, 1);
    CHECK_EQ_INT(t.type, TOK_IF);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_ELSE);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_WHILE);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_FOR);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_FN);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_RETURN);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_TRUE);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_FALSE);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_EOF);

    t = next_of(">= <= == != && || += -=", &lx, 1);
    CHECK_EQ_INT(t.type, TOK_GE);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_LE);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_EQ);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_NEQ);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_AND);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_OR);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_PLUS_ASSIGN);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_MINUS_ASSIGN);
}

static void test_cadenas_comentarios_lineas(void)
{
    struct soma_lexer lx;
    struct soma_token t;

    t = next_of("// comentario\nprint(\"hola\");", &lx, 1);
    CHECK_EQ_INT(t.type, TOK_PRINT);
    CHECK_EQ_INT(t.line, 2);
    CHECK_EQ_INT(next_of(0, &lx, 0).type, TOK_LPAREN);
    t = next_of(0, &lx, 0);
    CHECK_EQ_INT(t.type, TOK_STRING);
    CHECK_STR(t.text, "hola");
}

static void test_vacio_y_eof_repetido(void)
{
    struct soma_lexer lx;
    struct soma_token t;
    int i;

    t = next_of("", &lx, 1);
    CHECK_EQ_INT(t.type, TOK_EOF);
    for (i = 0; i < 5; ++i) {
        t = next_of(0, &lx, 0);
        CHECK_EQ_INT(t.type, TOK_EOF);
    }
}

/* Robustez: el campo text[] mide 128 bytes; entradas largas no deben
 * escribir fuera de el (lo detecta AddressSanitizer al ejecutar `make test`). */
static void test_identificador_y_cadena_largos(void)
{
    static char big[4096];
    struct soma_lexer lx;
    struct soma_token t;
    size_t i;

    for (i = 0; i < 1000; ++i) big[i] = 'a';
    big[1000] = '\0';
    lexer_init(&lx, big, (uint32_t)strlen(big));
    t = lexer_next(&lx);
    /* Mas de 127 caracteres: el lexer debe rechazarlo (TOK_ERROR) o truncar. */
    CHECK(t.type == TOK_ERROR || t.type == TOK_IDENT);
    CHECK(strlen(t.text) < sizeof(t.text));

    big[0] = '"';
    for (i = 1; i < 1000; ++i) big[i] = 'b';
    big[1000] = '"';
    big[1001] = '\0';
    lexer_init(&lx, big, (uint32_t)strlen(big));
    t = lexer_next(&lx);
    CHECK(t.type == TOK_ERROR || t.type == TOK_STRING);
    CHECK(strlen(t.text) < sizeof(t.text));
}

/* Fuzz determinista: bytes pseudoaleatorios; debe terminar siempre y sin
 * accesos fuera de rango (src se copia a un bloque exacto para que ASan vea
 * cualquier lectura mas alla de `len`). */
static void test_fuzz_determinista(void)
{
    uint32_t seed = 0xC0FFEEu;
    int iter;

    for (iter = 0; iter < 2000; ++iter) {
        uint32_t len = 1 + (seed >> 8) % 200;
        char *buf = (char *)malloc(len);
        struct soma_lexer lx;
        uint32_t guard = 0;
        uint32_t i;

        for (i = 0; i < len; ++i) {
            seed = seed * 1664525u + 1013904223u;
            buf[i] = (char)(seed >> 24);
        }
        lexer_init(&lx, buf, len);
        for (;;) {
            struct soma_token t = lexer_next(&lx);
            if (t.type == TOK_EOF) break;
            if (++guard > len + 8) { CHECK(!"el lexer no avanza (bucle infinito)"); break; }
        }
        free(buf);
    }
    CHECK(1);
}

int main(void)
{
    test_basico();
    test_palabras_clave_y_operadores();
    test_cadenas_comentarios_lineas();
    test_vacio_y_eof_repetido();
    test_identificador_y_cadena_largos();
    test_fuzz_determinista();
    TEST_MAIN_END("lexer");
}
