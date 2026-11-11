/* Pruebas en host de soma_str.h (buffers acotados). */
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "soma_str.h"

static void test_append_y_truncado(void)
{
    char store[8];
    soma_sbuf_t sb;

    soma_sbuf_init(&sb, store, sizeof(store));
    CHECK_STR(store, "");
    CHECK_EQ_INT(soma_sbuf_append_str(&sb, "abc"), 1);
    CHECK_STR(store, "abc");
    /* No cabe: se trunca, se informa con 0 y siempre queda terminador NUL. */
    CHECK_EQ_INT(soma_sbuf_append_str(&sb, "defghij"), 0);
    CHECK_STR(store, "abcdefg");
    CHECK_EQ_INT(soma_sbuf_avail(&sb), 0);
    CHECK_EQ_INT(soma_sbuf_append_char(&sb, 'x'), 0);
    CHECK_EQ_INT(store[7], 0);
}

static void test_copy_y_clear(void)
{
    char store[16];
    soma_sbuf_t sb;

    soma_sbuf_init(&sb, store, sizeof(store));
    soma_sbuf_copy(&sb, "uno");
    soma_sbuf_copy(&sb, "dos");
    CHECK_STR(store, "dos");
    soma_sbuf_clear(&sb);
    CHECK_STR(store, "");
    CHECK_EQ_INT(sb.len, 0);
}

static void test_u32(void)
{
    char store[32];
    soma_sbuf_t sb;

    soma_sbuf_init(&sb, store, sizeof(store));
    soma_sbuf_append_u32(&sb, 0);
    soma_sbuf_append_char(&sb, ',');
    soma_sbuf_append_u32(&sb, 4294967295u);
    CHECK_STR(store, "0,4294967295");
}

static void test_capacidad_cero_y_nulos(void)
{
    soma_sbuf_t sb;
    char store[1];

    soma_sbuf_init(&sb, store, 0);
    CHECK_EQ_INT(soma_sbuf_append_char(&sb, 'a'), 0);
    soma_sbuf_init(&sb, store, 1);
    CHECK_EQ_INT(soma_sbuf_append_str(&sb, "a"), 0);
    CHECK_STR(store, "");
    CHECK_EQ_INT(soma_sbuf_eq(0, "a"), 0);
    CHECK_EQ_INT(soma_sbuf_ends_with(0, "a"), 0);
}

static void test_eq_y_ends_with(void)
{
    CHECK_EQ_INT(soma_sbuf_eq("abc", "abc"), 1);
    CHECK_EQ_INT(soma_sbuf_eq("abc", "abd"), 0);
    CHECK_EQ_INT(soma_sbuf_eq("abc", "ab"), 0);
    CHECK_EQ_INT(soma_sbuf_ends_with("pong.SOMA", ".soma"), 1);
    CHECK_EQ_INT(soma_sbuf_ends_with("pong.txt", ".soma"), 0);
    CHECK_EQ_INT(soma_sbuf_ends_with("a", ".soma"), 0);
}

int main(void)
{
    test_append_y_truncado();
    test_copy_y_clear();
    test_u32();
    test_capacidad_cero_y_nulos();
    test_eq_y_ends_with();
    TEST_MAIN_END("soma_str");
}
