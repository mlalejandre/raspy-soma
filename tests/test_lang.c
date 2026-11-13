/* Pruebas en host del compilador + VM de SOMA-LANG (lang.c, vm.c, lexer.c). */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "lang.h"
#include "vm.h"

static struct soma_vm g_vm;
static uint8_t g_bc[65536];

/* Compila y ejecuta; devuelve el codigo de la VM (-100 si no compila). */
static int run_prog(const char *src)
{
    uint32_t bl = 0;
    int rc;

    host_out_reset();
    if (lang_compile(src, (uint32_t)strlen(src), g_bc, sizeof(g_bc), &bl) != 0)
        return -100;
    vm_init(&g_vm);
    rc = vm_run(&g_vm, g_bc, bl);
    vm_release(&g_vm);
    return rc;
}

#define EXPECT_OUT(src, expected) do { \
    int _rc = run_prog(src); \
    CHECK_EQ_INT(_rc, 0); \
    CHECK_STR(host_out(), expected); \
} while (0)

static void test_aritmetica(void)
{
    EXPECT_OUT("print(2+3*4); print((2+3)*4); print(10%3); print(7/2); print(-5+2);",
               "14\n20\n1\n3\n-3\n");
    /* Desbordamiento: envuelve (complemento a dos) sin comportamiento indefinido. */
    EXPECT_OUT("let t = 9223372036854775807; print(t+1);", "-9223372036854775808\n");
    EXPECT_OUT("let t = 0-9223372036854775807-1; print(t); print(t-1);",
               "-9223372036854775808\n9223372036854775807\n");
    EXPECT_OUT("let t = 4611686018427387904; print(t*2); print(t*4);",
               "-9223372036854775808\n0\n");
}

/* Literales fuera de rango: no deben provocar comportamiento indefinido. */
static void test_literales_enormes(void)
{
    CHECK_EQ_INT(run_prog("print(9999999999999999999999);"), 0);
    CHECK_EQ_INT(run_prog("print(0xFFFFFFFFFFFFFFFFFF);"), 0);
    EXPECT_OUT("print(0xFF); print(0x10);", "255\n16\n");
}

static void test_cadenas_y_logica(void)
{
    EXPECT_OUT("print(\"a\"+\"b\"); print(\"n=\"+5);", "ab\nn=5\n");
    EXPECT_OUT("print(\"abc\" == \"abc\"); print(1<2 && 2<1); print(!false);", "1\n0\n1\n");
    EXPECT_OUT("let s=\"hola\"; print(s.len()); print(substr(s,1,2));", "4\nol\n");
}

static void test_control_de_flujo(void)
{
    EXPECT_OUT("let i=0; while (i<3) { print(i); i+=1; }", "0\n1\n2\n");
    EXPECT_OUT("for (let i=0;i<5;i+=1){ if (i==1) { continue; } if (i==3) { break; } print(i); }",
               "0\n2\n");
    EXPECT_OUT("if (1 > 2) { print(\"a\"); } else { print(\"b\"); }", "b\n");
}

static void test_funciones_y_arrays(void)
{
    EXPECT_OUT("fn fib(n){ if (n<2) { return n; } return fib(n-1)+fib(n-2); } print(fib(15));",
               "610\n");
    EXPECT_OUT("let a=[1,2,3]; a.push(4); print(a.len()); print(a[3]); a[0]=9; print(a[0]);",
               "4\n4\n9\n");
}

static void test_funciones_basicas(void)   /* PARCHE 101 */
{
    EXPECT_OUT("print(abs(0-7)); print(min(3,9)); print(max(3,9)); print(clamp(15,0,10)); print(clamp(0-5,0,10));", "7\n3\n9\n10\n0\n");
    EXPECT_OUT("print(sqrt(144)); print(sqrt(50)); print(sqrt(0)); print(sqrt(0-4));", "12\n7\n0\n0\n");
    EXPECT_OUT("print(find(\"hola mundo\",\"mundo\")); print(find(\"hola\",\"x\")); print(find(\"abc\",\"\"));", "5\n-1\n0\n");
    EXPECT_OUT("print(chr(65)+chr(66)); print(ord(\"A\",0)); print(ord(\"hola\",2)); print(ord(\"hi\",9));", "AB\n65\n108\n0\n");
    EXPECT_OUT("fn min(a,b){ return 42; } print(min(1,2));", "42\n");   /* una funcion propia manda sobre la del sistema */
}

static void test_errores_de_ejecucion_controlados(void)
{
    CHECK_EQ_INT(run_prog("print(1/0);"), -1);
    CHECK(strstr(host_out(), "Division por cero") != 0);
    CHECK_EQ_INT(run_prog("print(5%0);"), -1);
    CHECK(strstr(host_out(), "Modulo por cero") != 0);
    /* Recursion infinita: debe abortar con error, no desbordar la pila del host. */
    CHECK_EQ_INT(run_prog("fn f(n){ return f(n+1); } print(f(0));"), -1);
    CHECK(strstr(host_out(), "marcos") != 0);
}

static void test_errores_de_compilacion(void)
{
    uint32_t bl = 0;
    const char *bad[] = {
        "let x = ;",
        "print(\"sin cerrar);",
        "if (1) { print(1);",
        "fn (",
        "let = 3;",
    };
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        int rc = lang_compile(bad[i], (uint32_t)strlen(bad[i]), g_bc, sizeof(g_bc), &bl);
        CHECK(rc != 0);
        CHECK(lang_get_last_error_line() >= 1);
        CHECK(lang_get_last_error_msg() != 0 && lang_get_last_error_msg()[0] != 0);
    }
    /* La linea del error debe ser la correcta en programas multilinea. */
    {
        const char *src = "print(1);\nprint(2);\nlet x = ;\n";
        CHECK(lang_compile(src, (uint32_t)strlen(src), g_bc, sizeof(g_bc), &bl) != 0);
        CHECK_EQ_INT(lang_get_last_error_line(), 3);
    }
}

/* El bytecode de salida cabe en el buffer pedido: con un buffer minusculo
 * el compilador debe fallar limpiamente, no escribir fuera. */
static void test_buffer_de_salida_pequeno(void)
{
    uint8_t tiny[4];
    uint32_t bl = 0;
    const char *src = "print(1); print(2); print(3); print(4);";

    CHECK(lang_compile(src, (uint32_t)strlen(src), tiny, sizeof(tiny), &bl) != 0);
}

/* Fuzz determinista: sopa de tokens; el compilador debe terminar sin fallos de memoria. */
static void test_fuzz_compilador(void)
{
    static const char *tok[] = {
        "let", "x", "y", "=", ";", "(", ")", "{", "}", "[", "]", "fn", "if", "else", "while",
        "for", "return", "print", "1", "42", "\"s\"", "+", "-", "*", "/", "%", "==", "<", ">",
        "&&", "||", "!", ",", ".", "len", "push", "break", "continue", "win", "fb", "sys",
        "//c\n", "\"", "'", "@", "0x", "9999999999999999999999", "0xFFFFFFFFFFFFFFFFFF"
    };
    const size_t nt = sizeof(tok) / sizeof(tok[0]);
    uint32_t seed = 0xBADC0DEu;
    int it;

    for (it = 0; it < 3000; ++it) {
        char src[512];
        size_t n = 0;
        uint32_t bl = 0;
        int k, cnt;

        seed = seed * 1664525u + 1013904223u;
        cnt = 1 + (int)((seed >> 16) % 40);
        for (k = 0; k < cnt; ++k) {
            const char *t;
            size_t tl;
            seed = seed * 1664525u + 1013904223u;
            t = tok[(seed >> 8) % nt];
            tl = strlen(t);
            if (n + tl + 2 >= sizeof(src)) break;
            memcpy(src + n, t, tl); n += tl; src[n++] = ' ';
        }
        src[n] = 0;
        (void)lang_compile(src, (uint32_t)n, g_bc, sizeof(g_bc), &bl);
    }
    CHECK(1);
}

/* lang_lint: errores duros (hard) y avisos, y limite de mensajes. */
static void test_lint(void)
{
    struct soma_lint_report r;
    static char big[4000];
    size_t i;
    const char *s;

    s = "print(1);";
    memset(&r, 0, sizeof(r));
    CHECK_EQ_INT(lang_lint(s, (uint32_t)strlen(s), &r), 0);
    CHECK_EQ_INT(r.count, 0);

    s = "# comentario estilo python\nprint(1);";
    memset(&r, 0, sizeof(r));
    CHECK(lang_lint(s, (uint32_t)strlen(s), &r) >= 1);
    CHECK(r.msg[0][0] == 'E');

    s = "print(1); ...";
    memset(&r, 0, sizeof(r));
    CHECK(lang_lint(s, (uint32_t)strlen(s), &r) >= 1);

    s = "while (true) { let x = 1; }";                 /* aviso, no error duro */
    memset(&r, 0, sizeof(r));
    CHECK_EQ_INT(lang_lint(s, (uint32_t)strlen(s), &r), 0);
    CHECK_EQ_INT(r.count, 1);
    CHECK(r.msg[0][0] == 'W');

    memset(&r, 0, sizeof(r));
    CHECK(lang_lint("", 0, &r) >= 1);                  /* vacio = error */
    CHECK_EQ_INT(lang_lint("x", 1, 0), 0);             /* informe nulo tolerado */

    /* Cientos de errores: el informe se acota a 8 mensajes sin desbordar. */
    for (i = 0; i < sizeof(big) - 1; ++i) big[i] = (i % 2) ? '#' : '\n';
    big[sizeof(big) - 1] = 0;
    memset(&r, 0, sizeof(r));
    (void)lang_lint(big, (uint32_t)strlen(big), &r);
    CHECK(r.count <= 8);
}

static void test_autotests_internos(void)
{
    host_out_reset();
    CHECK_EQ_INT(lang_test_self(), 1);
    CHECK_EQ_INT(vm_test_self(), 1);
}

int main(void)
{
    lang_init();
    test_aritmetica();
    test_literales_enormes();
    test_cadenas_y_logica();
    test_control_de_flujo();
    test_funciones_y_arrays();
    test_funciones_basicas();
    test_errores_de_ejecucion_controlados();
    test_errores_de_compilacion();
    test_buffer_de_salida_pequeno();
    test_fuzz_compilador();
    test_lint();
    test_autotests_internos();
    TEST_MAIN_END("lang+vm");
}
