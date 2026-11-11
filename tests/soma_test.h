/* Mini framework de pruebas en host para SOMA (sin dependencias). */
#ifndef SOMA_TEST_H
#define SOMA_TEST_H

#include <stdio.h>

static int soma_t_checks = 0;
static int soma_t_fails = 0;

#define CHECK(cond) do { \
    soma_t_checks++; \
    if (!(cond)) { \
        soma_t_fails++; \
        printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ_INT(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    soma_t_checks++; \
    if (_a != _b) { \
        soma_t_fails++; \
        printf("  FALLO %s:%d: %s == %s (%lld != %lld)\n", \
               __FILE__, __LINE__, #a, #b, _a, _b); \
    } \
} while (0)

#define CHECK_STR(a, b) do { \
    const char *_a = (a), *_b = (b); \
    soma_t_checks++; \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        soma_t_fails++; \
        printf("  FALLO %s:%d: %s == %s (\"%s\" != \"%s\")\n", \
               __FILE__, __LINE__, #a, #b, _a ? _a : "(null)", _b ? _b : "(null)"); \
    } \
} while (0)

#define TEST_MAIN_END(name) \
    printf("[%s] %d comprobaciones, %d fallos\n", name, soma_t_checks, soma_t_fails); \
    return soma_t_fails ? 1 : 0

#endif
