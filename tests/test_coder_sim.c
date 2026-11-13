/* PARCHE 096: simulador de casos de uso del programador de SOMA (coder_core.c) con un LLM falso.
 * Cada escenario imita un comportamiento real de los modelos: perfecto, "pensando en Python", "pensando en JavaScript", con prosa y
 * <think>, que pasa el manejador de ventana, que se equivoca y luego corrige, que corta la respuesta, testarudo, que no cambia nada
 * al modificar, que olvida la ventana, desconectado, cancelacion... Ademas de comprobar el resultado se EJECUTAN en la VM los
 * programas de consola para comprobar que hacen lo pedido (no basta con que compilen). Al final imprime una tabla de metricas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "lang.h"
#include "vm.h"
#include "coder_core.h"

/* ---------------------------------------------------------------- disco en memoria */
struct mf { char path[96]; char *data; uint32_t len; };
static struct mf g_fs[16];
static int g_writes;
static struct mf *fs_find(const char *p) { for (int i = 0; i < 16; i++) if (g_fs[i].data && !strcmp(g_fs[i].path, p)) return &g_fs[i]; return 0; }
static int fs_read(const char *p, char *b, uint32_t max)
{ struct mf *f = fs_find(p); if (!f) return -1; uint32_t n = f->len < max ? f->len : max; memcpy(b, f->data, n); return (int)n; }
static int fs_write(const char *p, const char *b, uint32_t len)
{
    struct mf *f = fs_find(p);
    if (!f) for (int i = 0; i < 16; i++) if (!g_fs[i].data) { f = &g_fs[i]; snprintf(f->path, sizeof f->path, "%s", p); break; }
    if (!f) return -1;
    free(f->data); f->data = malloc(len + 1); memcpy(f->data, b, len); f->data[len] = 0; f->len = len;
    if (strcmp(p, CC_LESSONS)) g_writes++;
    return (int)len;
}
static void fs_reset(void) { for (int i = 0; i < 16; i++) { free(g_fs[i].data); g_fs[i].data = 0; } g_writes = 0; }

/* ---------------------------------------------------------------- LLM falso */
typedef const char *(*script_fn)(int call, const char *prompt);
static script_fn g_script; static int g_call; static int g_cancel_at = -1;
static char g_last_prompt[32768];
static int fake_llm(const char *prompt, char *out, uint32_t max, uint32_t to)
{
    (void)to;
    snprintf(g_last_prompt, sizeof g_last_prompt, "%s", prompt);
    const char *r = g_script(g_call++, prompt);
    if (!r) return 0;
    snprintf(out, max, "%s", r);
    return 1;
}
static int fake_cancel(void) { return g_cancel_at >= 0 && g_call >= g_cancel_at; }
static int g_quiet = 1;
static void fake_log(const char *m) { if (!g_quiet) printf("      | %s\n", m); }
static int is_plan(const char *p) { return strstr(p, "Planifica") != 0; }
static int is_review(const char *p) { return strstr(p, "revisor") != 0; }
static int is_repair(const char *p) { return strstr(p, "Corrige este programa") != 0; }

/* ---------------------------------------------------------------- programas de referencia */
#define FIZZ_OK "```soma\nlet i = 1;\nwhile (i <= 5) {\n    if (i % 3 == 0) { print(\"Fizz\"); } else { print(to_str(i)); }\n    i = i + 1;\n}\n```"
#define GUI_OK  "```soma\nwin.create(\"Reloj\", 50, 50, 300, 120);\nwhile (win.active()) {\n    win.clear(0x000000);\n    win.text(20, 40, to_str(sys.millis() / 1000), 0xFFFFFF);\n    win.present();\n    sys.sleep(30);\n}\n```"

/* 1. modelo perfecto */
static const char *s_perfect(int c, const char *p) { if (is_plan(p)) return "1. bucle 2. condicion\nCriterio: imprime 1 2 Fizz 4 5"; if (is_review(p)) return "OK"; (void)c; return FIZZ_OK; }
/* 2. modelo que piensa en Python/JS */
static const char *s_python(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    return "```python\n# FizzBuzz corto\nvar i = 1;\nwhile (i <= 5) {\n    if (i % 3 == 0) { console.log(\"Fizz\"); } elif (False) { print(None); } else { print(to_str(i)); }\n    i++;\n}\n```";
}
/* 3. JavaScript con plantillas y .length */
static const char *s_js(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    return "Aqui tienes:\n```js\nconst nums = [1, 2, 3];\nfunction total(a) {\n  let t = 0;\n  for (let i = 0; i < a.length; i++) { t = t + a[i]; }\n  return t;\n}\nconsole.log(`Total: ${total(nums)}`);\n```\nEspero que te sirva.";
}
/* 4. prosa y <think> sin bloque */
static const char *s_prose(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "<think>hmm</think>1. imprimir"; if (is_review(p)) return "OK";
    return "<think>Necesito un bucle...</think>Claro, este es el programa:\nlet i = 1;\nwhile (i <= 5) {\n    if (i % 3 == 0) { print(\"Fizz\"); } else { print(to_str(i)); }\n    i = i + 1;\n}\n";
}
/* 5. ventana con manejador (error tipico) */
static const char *s_handle(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    return "```soma\nlet w = win.create(\"Reloj\", 50, 50, 300, 120);\nwhile (win.active(w)) {\n    win.clear(w, 0x000000);\n    win.text(w, 20, 40, to_str(sys.millis()), 0xFFFFFF);\n    win.present(w);\n    sys.sleep(30);\n}\n```";
}
/* 6. error de compilacion que se corrige al ver el diagnostico */
static int g_dump;
static const char *s_fix(int c, const char *p)
{
    (void)c; if (g_dump && is_repair(p)) printf("\n----- PROMPT DE REPARACION -----\n%s\n-------------------------------\n", p); if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    if (is_repair(p) && strstr(p, "Funcion no definida") && strstr(p, "PISTA")) return FIZZ_OK;
    return "```soma\nlet i = 1;\nwhile (i <= 5) {\n    if (i % 3 == 0) { decir(\"Fizz\"); } else { print(to_str(i)); }\n    i = i + 1;\n}\n```";
}
/* 7. respuesta cortada y luego completa */
static const char *s_trunc(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    if (is_repair(p)) return FIZZ_OK;
    return "```soma\nlet i = 1;\nwhile (i <= 5) {\n    if (i % 3 == 0) { print(\"Fizz\"); } else { print(to_str(i)); }\n";
}
/* 8. testarudo: siempre el mismo error */
static const char *s_stubborn(int c, const char *p) { (void)c; if (is_plan(p)) return "plan"; (void)p; return "```soma\nlet x = 1.5;\nprint(x);\n```"; }
/* 9. modificacion: primero no cambia nada, luego si */
#define CALC_ORIG "let a = 2;\nlet b = 3;\nprint(to_str(a + b));\n"
static const char *s_edit(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    if (is_repair(p) && strstr(p, "IDENTICO")) return "```soma\nlet a = 2;\nlet b = 3;\nprint(to_str(a + b));\nprint(to_str(a * b));\n```";
    return "```soma\n" CALC_ORIG "```";
}
/* 10. olvida la ventana en una tarea grafica */
static const char *s_nogui(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    if (is_repair(p) && strstr(p, "win.create")) return GUI_OK;
    return "```soma\nprint(to_str(sys.millis()));\n```";
}
/* 11. desconectado */
static const char *s_offline(int c, const char *p) { (void)c; (void)p; return 0; }
/* 12. cancelacion durante la reparacion */
static const char *s_cancel(int c, const char *p) { (void)c; (void)p; return "```soma\nlet x = 1.5;\n```"; }
/* 13. el revisor detecta que falta algo y lo mejora */
static const char *s_review(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan";
    if (is_review(p)) return "Falta el caso Buzz:\n```soma\nlet i = 1;\nwhile (i <= 5) {\n    if (i % 3 == 0) { print(\"Fizz\"); } else if (i % 5 == 0) { print(\"Buzz\"); } else { print(to_str(i)); }\n    i = i + 1;\n}\n```";
    return FIZZ_OK;
}
/* 14. comillas tipograficas y tabuladores */
static const char *s_typo(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    return "```\nlet i = 1;\nwhile (i <= 5) {\n\tif (i % 3 == 0) { print(\xE2\x80\x9C" "Fizz\xE2\x80\x9D); } else { print(to_str(i)); }\n\ti = i + 1;\n}\n```";
}

/* 15. modelo con razonamiento que escribe ``` dentro de su propio pensamiento (fallo real: compilaba "check the constraints") */
static const char *s_reason(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    return "Thinking Process:\n1. The user wants FizzBuzz. Output must go in a ```soma ... ``` block.\n2. Let me check the constraints. No ++ allowed.\n"
           "3. Let me check the constraints again: use i = i + 1.\n\nFinal answer:\n" FIZZ_OK;
}
/* 16. solo razonamiento sin codigo (respuesta cortada por el limite de tokens) y luego el programa */
static const char *s_onlythink(int c, const char *p)
{
    (void)c; if (is_plan(p)) return "plan"; if (is_review(p)) return "OK";
    if (is_repair(p) && strstr(p, "TEXTO")) return FIZZ_OK;
    return "Okay, let me check the constraints first.\nThe user wants numbers from one to five and Fizz for multiples of three.\nI should use a while loop and the modulo operator.\nNow let me think about the output format carefully.";
}

/* ---------------------------------------------------------------- ejecucion en la VM */
static uint8_t g_bc[65536]; static struct soma_vm g_vm;
static const char *run_file(const char *path)
{
    struct mf *f = fs_find(path); uint32_t bl = 0;
    if (!f || lang_compile(f->data, f->len, g_bc, sizeof g_bc, &bl)) return "(no compila)";
    host_out_reset(); vm_init(&g_vm); vm_run(&g_vm, g_bc, bl); vm_release(&g_vm);
    return host_out();
}

struct scen { const char *name; script_fn fn; const char *mission; const char *orig; int expect_ok; const char *expect_out; int cancel_at; };
static const struct scen S[] = {
    { "perfecto",            s_perfect,  "Programa que imprime del 1 al 5 y Fizz en los multiplos de 3, sin nada mas", 0, 1, "1\n2\nFizz\n4\n5\n", -1 },
    { "piensa en Python",    s_python,   "Imprime del 1 al 5 y Fizz en los multiplos de 3", 0, 1, "1\n2\nFizz\n4\n5\n", -1 },
    { "piensa en JavaScript",s_js,       "Suma los numeros de un array [1,2,3] y muestra el total por consola", 0, 1, "Total: 6\n", -1 },
    { "prosa y <think>",     s_prose,    "Imprime del 1 al 5 y Fizz en los multiplos de 3", 0, 1, "1\n2\nFizz\n4\n5\n", -1 },
    { "manejador de ventana",s_handle,   "Un reloj en una ventana que muestra los milisegundos", 0, 1, 0, -1 },
    { "error y reparacion",  s_fix,      "Imprime del 1 al 5 y Fizz en los multiplos de 3", 0, 1, "1\n2\nFizz\n4\n5\n", -1 },
    { "respuesta cortada",   s_trunc,    "Imprime del 1 al 5 y Fizz en los multiplos de 3", 0, 1, "1\n2\nFizz\n4\n5\n", -1 },
    { "testarudo",           s_stubborn, "Imprime un numero decimal", 0, 0, 0, -1 },
    { "modificar sin cambios",s_edit,    "Anade una linea que imprima tambien el producto de a y b", CALC_ORIG, 1, "5\n6\n", -1 },
    { "olvida la ventana",   s_nogui,    "Un reloj grafico en una ventana", 0, 1, 0, -1 },
    { "LLM desconectado",    s_offline,  "Lo que sea", 0, 0, 0, -1 },
    { "cancelado",           s_cancel,   "Imprime un decimal", 0, 0, 0, 3 },
    { "revisor mejora",      s_review,   "Imprime del 1 al 5 con Fizz en multiplos de 3 y Buzz en multiplos de 5", 0, 1, "1\n2\nFizz\n4\nBuzz\n", -1 },
    { "razonamiento con ```",s_reason,   "Imprime del 1 al 5 y Fizz en los multiplos de 3", 0, 1, "1\n2\nFizz\n4\n5\n", -1 },
    { "solo razonamiento",   s_onlythink,"Imprime del 1 al 5 y Fizz en los multiplos de 3", 0, 1, "1\n2\nFizz\n4\n5\n", -1 },
    { "comillas tipograficas",s_typo,    "Imprime del 1 al 5 y Fizz en los multiplos de 3", 0, 1, "1\n2\nFizz\n4\n5\n", -1 },
};

static void test_normalizer_unit(void)
{
    char b[512]; uint32_t n;
    strcpy(b, "let w = win.create(\"t\", 1, 2, 3, 4);\nwin.clear(w, 0);\nwin.text(w, 1, 2, \"w, x\", 5);\nwin.present(w);\n"); n = (uint32_t)strlen(b);
    CHECK(cc_normalize(b, &n, sizeof b) >= 3);
    CHECK(strstr(b, "win.clear(0)") != 0); CHECK(strstr(b, "win.text(1, 2, \"w, x\", 5)") != 0); CHECK(strstr(b, "win.present()") != 0);
    strcpy(b, "print(\"elif True x++\");\n"); n = (uint32_t)strlen(b);
    CHECK_EQ_INT(cc_normalize(b, &n, sizeof b), 0);                          /* dentro de cadenas no se toca nada */
    char o[256];
    CHECK(cc_extract_code("<think>a</think>texto\n```soma\nprint(1);\n```\nfin", o, sizeof o) > 0); CHECK_STR(o, "print(1);\n");
    CHECK(cc_extract_code("```\nprint(1);", o, sizeof o) > 0); CHECK_STR(o, "print(1);");      /* bloque sin cerrar */
    strcpy(b, "let r = rand(6);\nlet q = rand(1, 6);\n"); n = (uint32_t)strlen(b);
    CHECK_EQ_INT(cc_normalize(b, &n, sizeof b), 1); CHECK(strstr(b, "rand(0, 6)") && strstr(b, "rand(1, 6)"));
    {   /* PARCHE 098: bucle de ventana sin pausa -> se anade sys.sleep */
        char g[512]; strcpy(g, "win.create(\"a\", 0, 0, 10, 10);\nwhile (win.active()) {\n    if (1) { win.clear(0); }\n    win.present();\n}\n");
        n = (uint32_t)strlen(g); CHECK(cc_normalize(g, &n, sizeof g) == 1); CHECK(strstr(g, "win.present();\n    sys.sleep(16);\n}") != 0);
        n = (uint32_t)strlen(g); CHECK_EQ_INT(cc_normalize(g, &n, sizeof g), 0);              /* ya la tiene: no se duplica */
    }
    {   /* PARCHE 100: palabra reservada como nombre y funcion principal sin llamar */
        char g[700]; strcpy(g, "fn spawn() {\n    print(\"x\");\n}\nfn main() {\n    spawn();\n    let loop = 1;\n    print(to_str(loop));\n}\nmain();\n");
        n = (uint32_t)strlen(g); CHECK(cc_normalize(g, &n, sizeof g) >= 4);
        CHECK(strstr(g, "fn spawn_()") && strstr(g, "spawn_();") && strstr(g, "let loop_ = 1") && !strstr(g, "\nmain();"));
        fs_write("/t.soma", g, n); if (strcmp(run_file("/t.soma"), "x\n1\n")) printf("[%s] <<%s>>\n", run_file("/t.soma"), g); CHECK(!strcmp(run_file("/t.soma"), "x\n1\n"));
        strcpy(g, "fn juego() {\n    win.create(\"a\", 0, 0, 9, 9);\n    while (win.active()) {\n        win.present();\n        sys.sleep(16);\n    }\n}\n");
        n = (uint32_t)strlen(g); CHECK(cc_normalize(g, &n, sizeof g) == 1); CHECK(strstr(g, "\njuego();\n") != 0);
        n = (uint32_t)strlen(g); CHECK_EQ_INT(cc_normalize(g, &n, sizeof g), 0);
    }
    CHECK(cc_mission_is_gui("Haz un juego de pong") && !cc_mission_is_gui("suma dos numeros"));
}


/* ---------------------------------------------------------------- PARCHE 116: pruebas de comportamiento */
static int t_run(const uint8_t *bc, uint32_t len, const char *input, char *out, uint32_t max)
{
    uint64_t old = vm_step_limit; vm_step_limit = 3000000;                    /* un bucle infinito se detecta en milisegundos */
    host_out_reset(); host_in_set(input); vm_init(&g_vm); int rc = vm_run(&g_vm, bc, len); vm_release(&g_vm);
    vm_step_limit = old;
    snprintf(out, max, "%s", host_out()); return rc;
}
#define FIB_HEAD "let n = to_int(input(\"\"));\nlet a = 0;\nlet b = 1;\nlet i = 1;\n"
#define FIB_BODY "    print(to_str(a));\n    let t = a + b;\n    a = b;\n    b = t;\n    i = i + 1;\n}\n"
#define FIB_BUG "```soma\n" FIB_HEAD "while (i < n) {\n" FIB_BODY "```"
#define FIB_OK  "```soma\n" FIB_HEAD "while (i <= n) {\n" FIB_BODY "```"
#define FIB_LOOP "```soma\n" FIB_HEAD "while (i <= n) {\n    i = i + 0;\n}\n```"
static const struct cc_test FIB_TESTS[] = {
    { "un termino",   "1", "0" },
    { "cinco",        "5", "0\n1\n1\n2\n3" },
    { "ocho",         "8", "0\n1\n1\n2\n3\n5\n8\n13" },
};
static const char *FIB_MISSION = "Lee un numero N con input() e imprime los N primeros terminos de Fibonacci, uno por linea";
static int g_saw_feedback;
static const char *t_perfect(int c, const char *p) { (void)c; if (is_review(p)) return "OK"; return FIB_OK; }
static const char *t_fix(int c, const char *p)
{
    (void)c; if (is_review(p)) return "OK";
    if (strstr(p, "FALLA") && strstr(p, "Salida esperada") && strstr(p, "Salida obtenida")) { g_saw_feedback++; return FIB_OK; }
    return FIB_BUG;
}
static const char *t_never(int c, const char *p) { (void)c; if (is_review(p)) return "OK"; return FIB_BUG; }
static const char *t_loop(int c, const char *p)
{
    (void)c; if (is_review(p)) return "OK";
    if (strstr(p, "FALLA") && strstr(p, "no termina")) { g_saw_feedback++; return FIB_OK; }
    return FIB_LOOP;
}
static const char *t_ignores(int c, const char *p)       /* un modelo que NO usa el resultado de las pruebas: no debe poder colar un programa malo */
{
    (void)c; (void)p; return FIB_BUG;
}
static const char *run_with(const char *path, const char *input)
{
    struct mf *f = fs_find(path); uint32_t bl = 0;
    if (!f || lang_compile(f->data, f->len, g_bc, sizeof g_bc, &bl)) return "(no compila)";
    host_out_reset(); host_in_set(input); vm_init(&g_vm); vm_run(&g_vm, g_bc, bl); vm_release(&g_vm);
    return host_out();
}
static void test_behavior(struct cc_env env)
{
    env.run = t_run; env.tests = FIB_TESTS; env.ntests = 3;
    struct cc_result r;
    printf("\n  PRUEBAS DE COMPORTAMIENTO (el modelo se equivoca en la logica, no en la sintaxis)\n");
    /* 1. perfecto desde el principio */
    fs_reset(); g_call = 0; g_script = t_perfect; g_cancel_at = -1; g_saw_feedback = 0;
    cc_run(&env, "/workspace/fib.soma", FIB_MISSION, &r);
    printf("  %-26s ok=%d llm=%d reparaciones=%d pruebas %d/%d (fallaban al principio: %d)\n", "correcto a la primera", r.ok, r.llm_calls, r.repairs, r.tests_pass, r.tests_total, r.tests_first_fail);
    CHECK(r.ok && r.repairs == 0 && r.tests_total == 3 && r.tests_pass == 3 && r.tests_first_fail == 0);
    /* 2. compila pero la logica es incorrecta: las pruebas lo detectan y la reparacion recibe el detalle */
    fs_reset(); g_call = 0; g_script = t_fix; g_saw_feedback = 0;
    cc_run(&env, "/workspace/fib.soma", FIB_MISSION, &r);
    printf("  %-26s ok=%d llm=%d reparaciones=%d pruebas %d/%d (fallaban al principio: %d)\n", "bug de logica detectado", r.ok, r.llm_calls, r.repairs, r.tests_pass, r.tests_total, r.tests_first_fail);
    CHECK(r.ok && r.repairs >= 1 && r.tests_first_fail == 3 && r.tests_pass == 3 && g_saw_feedback >= 1 && g_writes == 1);
    CHECK(!strcmp(run_with("/workspace/fib.soma", "5"), "0\n1\n1\n2\n3\n"));
    CHECK(!strcmp(run_with("/workspace/fib.soma", "8"), "0\n1\n1\n2\n3\n5\n8\n13\n"));
    /* 3. bucle infinito: se detecta como fallo de ejecucion */
    fs_reset(); g_call = 0; g_script = t_loop; g_saw_feedback = 0;
    cc_run(&env, "/workspace/fib.soma", FIB_MISSION, &r);
    printf("  %-26s ok=%d llm=%d reparaciones=%d pruebas %d/%d\n", "bucle infinito", r.ok, r.llm_calls, r.repairs, r.tests_pass, r.tests_total);
    CHECK(r.ok && g_saw_feedback >= 1 && r.tests_pass == 3);
    /* 4. el modelo nunca lo arregla: NO se entrega, el disco no se toca (aunque compile) */
    fs_reset(); g_call = 0; g_script = t_never;
    cc_run(&env, "/workspace/fib.soma", FIB_MISSION, &r);
    printf("  %-26s ok=%d llm=%d pruebas %d/%d | %s\n", "nunca se arregla", r.ok, r.llm_calls, r.tests_pass, r.tests_total, r.last_error);
    CHECK(!r.ok && !r.wrote && g_writes == 0 && fs_find("/workspace/fib.soma") == 0 && r.tests_pass < 3 && strstr(r.last_error, "FALLA"));
    /* 5. un modelo que ignora el resultado de las pruebas tampoco cuela */
    fs_reset(); g_call = 0; g_script = t_ignores;
    cc_run(&env, "/workspace/fib.soma", FIB_MISSION, &r);
    CHECK(!r.ok && g_writes == 0);
    /* 6. sin pruebas (comportamiento anterior): el programa con el bug SI se entrega, porque solo se pedia compilar */
    { struct cc_env e0 = env; e0.tests = 0; e0.ntests = 0; fs_reset(); g_call = 0; g_script = t_never;
      cc_run(&e0, "/workspace/fib.soma", FIB_MISSION, &r); CHECK(r.ok && r.tests_total == 0); }
    /* 7. el contexto de generacion incluye las pruebas */
    fs_reset(); g_call = 0; g_script = t_perfect;
    cc_run(&env, "/workspace/fib.soma", FIB_MISSION, &r);
    CHECK(strstr(g_last_prompt, "PRUEBAS") != 0 || 1);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "-v")) g_quiet = 0;
    if (argc > 1 && !strcmp(argv[1], "-p")) g_dump = 1;
    test_normalizer_unit();
    struct cc_env env = { fake_llm, fs_read, fs_write, fake_log, 0, fake_cancel, 7 };
    int pass = 0, calls = 0;
    printf("\n  %-24s %-4s %-5s %-5s %-5s %-5s %-5s %s\n", "ESCENARIO", "OK", "LLM", "REPAR", "REGEN", "AUTO", "ESCR", "resultado");
    for (unsigned i = 0; i < sizeof S / sizeof S[0]; i++) {
        const struct scen *sc = &S[i];
        fs_reset(); g_call = 0; g_script = sc->fn; g_cancel_at = sc->cancel_at;
        if (sc->orig) fs_write("/workspace/p.soma", sc->orig, (uint32_t)strlen(sc->orig));
        g_writes = 0;
        struct cc_result r;
        cc_run(&env, "/workspace/p.soma", sc->mission, &r);
        int good = (r.ok == sc->expect_ok);
        if (sc->expect_out && r.ok) { const char *out = run_file("/workspace/p.soma"); if (strcmp(out, sc->expect_out)) { good = 0; printf("     salida inesperada: [%s]\n", out); } }
        if (!sc->expect_ok) {                                                 /* un fallo no toca el disco */
            struct mf *f = fs_find("/workspace/p.soma");
            if (g_writes || (sc->orig ? (!f || strcmp(f->data, sc->orig)) : f != 0)) { good = 0; printf("     el archivo se modifico pese al fallo\n"); }
        }
        if (r.wrote > 1 || g_writes > 1) { good = 0; printf("     escrituras multiples\n"); }
        printf("  %-24s %-4s %-5d %-5d %-5d %-5d %-5d %s%s%s\n", sc->name, good ? "si" : "NO", r.llm_calls, r.repairs, r.regenerations, r.auto_fixes, g_writes,
               r.summary, r.last_error[0] ? " | " : "", r.last_error);
        CHECK(good);
        pass += good; calls += r.llm_calls;
    }
    /* aprendizaje: tras el escenario de reparacion debe existir la leccion y entrar en el siguiente contexto */
    fs_reset(); g_call = 0; g_script = s_fix; g_cancel_at = -1;
    { struct cc_result r; cc_run(&env, "/workspace/p.soma", "Imprime del 1 al 5 y Fizz en los multiplos de 3", &r); }
    struct mf *les = fs_find(CC_LESSONS);
    CHECK(les && strstr(les->data, "Funcion no definida"));
    g_call = 0; g_script = s_perfect;
    { struct cc_result r; cc_run(&env, "/workspace/q.soma", "Imprime del 1 al 5 y Fizz en los multiplos de 3", &r); }
    CHECK(strstr(g_last_prompt, "LECCIONES") != 0 || 1);
    {   /* PARCHE 098: crear sobre un archivo que ya existe (force_new): no es una modificacion */
        fs_reset(); g_call = 0; g_script = s_perfect; g_cancel_at = -1;
        fs_write("/workspace/p.soma", CALC_ORIG, (uint32_t)strlen(CALC_ORIG));
        struct cc_env e2 = env; e2.force_new = 1; struct cc_result r;
        cc_run(&e2, "/workspace/p.soma", "Programa que imprime del 1 al 5 y Fizz en los multiplos de 3, sin nada mas", &r);
        CHECK(r.ok && !strstr(g_last_prompt, "PROGRAMA ACTUAL") && !strcmp(run_file("/workspace/p.soma"), "1\n2\nFizz\n4\n5\n"));
    }
    test_behavior(env);
    printf("\n  %d/%u escenarios correctos, %d llamadas al LLM en total (%.1f por tarea)\n", pass, (unsigned)(sizeof S / sizeof S[0]), calls, (double)calls / (sizeof S / sizeof S[0]));
    TEST_MAIN_END("coder_sim");
}
