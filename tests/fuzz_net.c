/* PARCHE 031: fuzzer de la pila de red (ARP, IPv4, ICMP, UDP, TCP con handshake y segmentos mutados) con ASAN+UBSAN.
 * Uso:  make fuzz-net   (FUZZ_ITERS=200000 FUZZ_SEED=3 make fuzz-net para pasadas largas) */
/* Fuzzer de la pila de red de SOMA (ARP, IPv4, ICMP, UDP, TCP): alimenta net_poll() con tramas mutadas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "net.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "mutex.h"
#include "thread.h"

/* ---- stubs del kernel ---- */
uint64_t tsc_ticks_per_ms = 1000000; static uint64_t g_ms = 1000;
uint64_t timer_get_uptime_ms(void) { return g_ms; }
void *kmalloc(size_t n) { return malloc(n ? n : 1); } void kfree(void *p) { free(p); }
void kheap_stats(size_t *u, size_t *f) { if (u) *u = 0; if (f) *f = 0; }
void pmm_get_stats(size_t *a, size_t *b, size_t *c) { if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
void kprint(const char *s) { (void)s; } void kprint_dec(uint32_t v) { (void)v; } void kprint_ip(const uint8_t *p) { (void)p; } void kprint_mac(const uint8_t *p) { (void)p; }
void kprint_hex32(uint32_t v) { (void)v; }
void kmutex_init(struct kmutex *m, const char *nm) { (void)m; (void)nm; } void kmutex_lock(struct kmutex *m) { (void)m; } void kmutex_unlock(struct kmutex *m) { (void)m; } int kmutex_lock_timeout(struct kmutex *m, uint32_t t) { (void)m; (void)t; return 1; }
static struct tcb g_tcb = { .tid = 1 };
struct tcb *thread_current(void) { return &g_tcb; }
struct tcb *thread_create(const char *n, thread_func_t f, void *a) { (void)n; (void)f; (void)a; return &g_tcb; }
void thread_yield(void) { g_ms++; } void thread_sleep(uint32_t ms) { g_ms += ms; }
int vfs_format_list_ext(char *o, uint32_t m, int x) { (void)o; (void)m; (void)x; return 0; }
int vfs_get_stats(void *a, void *b) { (void)a; (void)b; return 0; }
int vfs_read(const char *p, void *b, uint32_t m) { (void)p; (void)b; (void)m; return -1; }
int virtio_net_init(void) { return 1; }
static const uint8_t K_MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
const uint8_t *virtio_net_mac(void) { return K_MAC; }
void virtio_net_stats(uint32_t *a, uint32_t *b, uint64_t *c, uint64_t *d) { (void)a; (void)b; (void)c; (void)d; }

/* cola de entrada y captura de salida */
#define QN 64
static uint8_t q[QN][1600]; static uint16_t ql[QN]; static int qh, qt;
static uint8_t last_tx[1600]; static uint16_t last_tx_len; static int tx_count;
int virtio_net_poll(void *out, uint16_t max, uint16_t *len) {
    if (qh == qt) return 0; uint16_t n = ql[qh % QN]; if (n > max) n = max; memcpy(out, q[qh % QN], n); *len = n; qh++; return 1; }
int virtio_net_send(const void *f, uint16_t len) { if (len > sizeof last_tx) len = sizeof last_tx; memcpy(last_tx, f, len); last_tx_len = len; tx_count++; return 1; }
static void push(const uint8_t *f, uint16_t n) { if (qt - qh >= QN) return; if (n > 1600) n = 1600; memcpy(q[qt % QN], f, n); ql[qt % QN] = n; qt++; }
static void drain(void) { while (net_poll()) ; }

static uint64_t S = 88172645463325252ULL;
static uint32_t rnd(void) { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32_t)(S >> 11); }
static uint16_t cks(const uint8_t *d, int n, uint32_t init) { uint32_t s = init; for (int i = 0; i + 1 < n; i += 2) s += (d[i] << 8) | d[i + 1]; if (n & 1) s += d[n - 1] << 8; while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return (uint16_t)~s; }
static void w16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void w32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static uint32_t r32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static const uint8_t PEER[4] = { 10, 0, 2, 2 };
static const uint8_t PMAC[6] = { 0x52, 0x55, 10, 0, 2, 2 };

/* construye una trama Ethernet+IPv4+L4 valida */
static int build(uint8_t *f, uint8_t proto, const uint8_t *l4, int l4n) {
    memcpy(f, K_MAC, 6); memcpy(f + 6, PMAC, 6); w16(f + 12, 0x0800);
    uint8_t *ip = f + 14; ip[0] = 0x45; ip[1] = 0; w16(ip + 2, 20 + l4n); w16(ip + 4, rnd()); w16(ip + 6, 0); ip[8] = 64; ip[9] = proto; w16(ip + 10, 0);
    memcpy(ip + 12, PEER, 4); memcpy(ip + 16, net_ip, 4); w16(ip + 10, cks(ip, 20, 0)); memcpy(ip + 20, l4, l4n); return 14 + 20 + l4n; }
static void fix_l4(uint8_t *f, int n) {   /* recalcula cabeceras IP y TCP/UDP tras mutar */
    if (n < 34) return; uint8_t *ip = f + 14; int ihl = (ip[0] & 15) * 4; if (ihl < 20 || 14 + ihl > n) return;
    w16(ip + 10, 0); w16(ip + 10, cks(ip, ihl, 0));
    int l4n = n - 14 - ihl; if (l4n < 8 || (ip[9] != 6 && ip[9] != 17 && ip[9] != 1)) return; uint8_t *l4 = ip + ihl;
    if (ip[9] == 1) { w16(l4 + 2, 0); w16(l4 + 2, cks(l4, l4n, 0)); return; }
    int co = ip[9] == 6 ? 16 : 6; if (l4n < co + 2) return; w16(l4 + co, 0);
    uint32_t ps = 0; for (int i = 0; i < 4; i += 2) ps += (ip[12 + i] << 8) | ip[13 + i]; for (int i = 0; i < 4; i += 2) ps += (ip[16 + i] << 8) | ip[17 + i]; ps += ip[9] + l4n;
    w16(l4 + co, cks(l4, l4n, ps)); }

static int tcp_seg(uint8_t *f, uint16_t sp, uint16_t dp, uint32_t seq, uint32_t ack, uint8_t fl, const uint8_t *data, int dn) {
    uint8_t t[1500]; memset(t, 0, 20); w16(t, sp); w16(t + 2, dp); w32(t + 4, seq); w32(t + 8, ack); t[12] = 5 << 4; t[13] = fl; w16(t + 14, 8192);
    if (dn > 1400) dn = 1400; memcpy(t + 20, data, dn); int n = build(f, 6, t, 20 + dn); fix_l4(f, n); return n; }
static int udp_dg(uint8_t *f, uint16_t sp, uint16_t dp, const uint8_t *data, int dn) {
    uint8_t t[1500]; w16(t, sp); w16(t + 2, dp); w16(t + 4, 8 + dn); w16(t + 6, 0); if (dn > 1400) dn = 1400; memcpy(t + 8, data, dn); int n = build(f, 17, t, 8 + dn); fix_l4(f, n); return n; }

static const char *REQS[] = { "GET / HTTP/1.1\r\nHost: x\r\n\r\n", "GET /../../etc/passwd HTTP/1.0\r\n\r\n", "POST /api HTTP/1.1\r\nContent-Length: 99999999\r\n\r\nabc",
    "GET /sys/telemetry.txt HTTP/1.1\r\nConnection: keep-alive\r\n\r\n", "\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03", "GET /%00%ff HTTP/1.1\r\nRange: bytes=-1--1\r\n\r\n" };

static void mutate(uint8_t *f, int *n) {
    int m = 1 + rnd() % 6;
    while (m--) {
        if (*n < 2) return; int p = rnd() % *n;
        switch (rnd() % 7) {
        case 0: f[p] ^= 1 << (rnd() % 8); break;
        case 1: f[p] = (uint8_t)rnd(); break;
        case 2: f[p] = (uint8_t[]){ 0, 1, 0x7f, 0x80, 0xff }[rnd() % 5]; break;
        case 3: if (*n > 20) *n = 14 + rnd() % (*n - 14); break;
        case 4: { int l = 1 + rnd() % 8; if (p + l < *n) { memmove(f + p, f + p + l, *n - p - l); *n -= l; } } break;
        case 5: if (*n < 1500) { int l = 1 + rnd() % 16; memmove(f + p + l, f + p, *n - p); for (int i = 0; i < l; i++) f[p + i] = (uint8_t)rnd(); *n += l; } break;
        case 6: if (*n > 40) { f[14 + 20 + (rnd() % 16)] = (uint8_t)rnd(); } break;
        }
    }
}

int main(int argc, char **argv) {
    long iters = atol(argv[1]); S ^= (uint64_t)atol(argv[2]) * 0x9E3779B97F4A7C15ULL;
    memcpy(net_ip, (uint8_t[]){ 10, 0, 2, 15 }, 4); memcpy(net_gateway, (uint8_t[]){ 10, 0, 2, 2 }, 4); net_init();
    int lh = tcp_listen(80), lh2 = tcp_listen(8080); (void)lh; (void)lh2; udp_bind(5353); udp_bind(68);
    uint8_t f[1700], seedf[1700];
    for (long it = 0; it < iters; it++) {
        g_ms += 3;
        int sc = rnd() % 6; int n;
        if (sc == 0) {   /* ARP */
            memset(f, 0xff, 6); memcpy(f + 6, PMAC, 6); w16(f + 12, 0x0806); uint8_t *a = f + 14; w16(a, 1); w16(a + 2, 0x0800); a[4] = 6; a[5] = 4; w16(a + 6, 1 + rnd() % 2);
            memcpy(a + 8, PMAC, 6); memcpy(a + 14, PEER, 4); memset(a + 18, 0, 6); memcpy(a + 24, net_ip, 4); n = 42; if (rnd() & 1) mutate(f, &n);
            push(f, (uint16_t)n);
        } else if (sc == 1) {   /* ICMP */
            uint8_t ic[64]; memset(ic, 0, sizeof ic); ic[0] = (uint8_t)(rnd() & 1 ? 8 : rnd()); w16(ic + 4, 0x534F); w16(ic + 6, rnd()); n = build(f, 1, ic, 8 + rnd() % 56); fix_l4(f, n); mutate(f, &n); if (rnd() % 4) fix_l4(f, n); push(f, (uint16_t)n);
        } else if (sc == 2) {   /* UDP */
            uint8_t d[600]; for (int i = 0; i < 600; i++) d[i] = (uint8_t)rnd(); uint16_t dp = (uint16_t[]){ 5353, 68, 53, 1234, 123 }[rnd() % 5];
            n = udp_dg(f, (uint16_t)(53 + (rnd() & 1)), dp, d, rnd() % 600); mutate(f, &n); if (rnd() % 4) fix_l4(f, n); push(f, (uint16_t)n);
        } else {   /* TCP: handshake completo y segmentos mutados */
            uint16_t sp = 40000 + rnd() % 8, dp = (uint16_t[]){ 80, 8080, 80, 22, 443 }[rnd() % 5]; uint32_t isn = rnd();
            tx_count = 0; n = tcp_seg(f, sp, dp, isn, 0, 0x02, (const uint8_t *)"", 0); push(f, (uint16_t)n); drain();
            uint32_t sseq = 0; if (tx_count && last_tx_len >= 14 + 20 + 20) sseq = r32(last_tx + 14 + 20 + 4);
            int steps = 1 + rnd() % 5;
            for (int s = 0; s < steps; s++) {
                const char *rq = REQS[rnd() % 6]; int rl = (int)strlen(rq); uint8_t fl = (uint8_t[]){ 0x18, 0x10, 0x11, 0x04, 0x19, 0x02, 0x14, 0x38 }[rnd() % 8];
                n = tcp_seg(f, sp, dp, isn + 1 + (s ? (uint32_t)(rnd() % 3 * rl) : 0), sseq + 1, fl, (const uint8_t *)rq, rl);
                if (rnd() % 3) { memcpy(seedf, f, n); mutate(f, &n); if (rnd() % 3) fix_l4(f, n); }
                push(f, (uint16_t)n); drain();
            }
        }
        drain();
        if (it % 5000 == 0) tcp_cleanup_for_thread(1);
    }
    fprintf(stderr, "fuzz_net: %ld iteraciones OK (tx=%d)\n", iters, tx_count);
    return 0;
}
