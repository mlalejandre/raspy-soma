/* Sustitutos de servicios del kernel para pruebas en host (comunes). */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "host_stubs.h"
#include "console.h"
#include "thread.h"
#include "idt.h"
#include "pmm.h"

static char     g_out[1 << 16];
static size_t   g_out_len;
static const char *g_in;
static uint64_t g_ms;

void host_out_reset(void) { g_out_len = 0; g_out[0] = 0; g_in = 0; }
const char *host_out(void) { return g_out; }
void host_in_set(const char *lines) { g_in = lines; }
void host_time_set(uint64_t ms) { g_ms = ms; }

void kputc(char c)
{
    if (g_out_len + 1 < sizeof(g_out)) { g_out[g_out_len++] = c; g_out[g_out_len] = 0; }
}
void kprint(const char *s) { while (s && *s) kputc(*s++); }
void kprint_dec(uint32_t v)
{
    char t[12]; int n = 0;
    if (!v) { kputc('0'); return; }
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) kputc(t[--n]);
}
void kprint_hex32(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) kputc(h[(v >> i) & 15]);
}
void kprint_hex8(uint8_t v)
{
    static const char h[] = "0123456789ABCDEF";
    kputc(h[v >> 4]); kputc(h[v & 15]);
}
int  kgetc_ready(void) { return g_in && *g_in; }
char kgetc(void) { return (g_in && *g_in) ? *g_in++ : 0; }
int  kgetline(char *buf, uint32_t max)
{
    uint32_t n = 0;
    while (g_in && *g_in && *g_in != '\n' && n + 1 < max) buf[n++] = *g_in++;
    if (g_in && *g_in == '\n') g_in++;
    if (max) buf[n] = 0;
    return (int)n;
}
void console_clear(void) { }

#ifndef SOMA_HOST_TEST   /* con SOMA_HOST_TEST se prueba el asignador real (src/core/mem.c) */
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void  kfree(void *p) { free(p); }
#endif

#ifndef SOMA_HOST_REAL_THREAD   /* con SOMA_HOST_REAL_THREAD se enlaza el planificador real (thread.c) */
static struct tcb g_tcb;
struct tcb *thread_current(void) { return &g_tcb; }
void host_agent_mode(int on) { g_tcb.agent_mode = on; }
void thread_sleep(uint32_t ms) { g_ms += ms; }
int  thread_format_table(char *out, uint32_t max) { if (max) out[0] = 0; return 0; }
#else
void host_agent_mode(int on) { thread_current()->agent_mode = on; }
#endif

uint64_t timer_get_uptime_ms(void) { return g_ms; }
void pmm_get_stats(size_t *f, size_t *u, size_t *t) { *f = 1000; *u = 24; *t = 1024; }
#ifndef SOMA_HOST_TEST
void kheap_stats(size_t *used, size_t *fr) { *used = 4096; *fr = 1u << 20; }
#endif
