#include <stdint.h>
#include <stddef.h>

#include "panic.h"
#include "console.h"
#include "thread.h"
#include "vmm.h"

#ifdef SOMA_HOST_PANIC                 /* pruebas en host: limites de .text simulados */
extern uint64_t host_text_lo, host_text_hi;
#define PANIC_TEXT_LO host_text_lo
#define PANIC_TEXT_HI host_text_hi
#else
extern char __text_start[];
extern char __text_end[];
#define PANIC_TEXT_LO ((uint64_t)(uintptr_t)__text_start)
#define PANIC_TEXT_HI ((uint64_t)(uintptr_t)__text_end)
#endif

#define PANIC_MAX_FRAME_JUMP 0x100000ULL    /* una pila de hilo mide 32 KiB; 1 MiB sobra */

int panic_walk_frames(uint64_t fp, uint64_t sp_floor, panic_read64_fn rd,
                      uint64_t *ret_out, int max)
{
    int n = 0;

    while (n < max) {
        uint64_t next, ret;

        if (fp == 0 || (fp & 7) != 0) break;
        if (sp_floor != 0 && fp < sp_floor) break;
        if (!rd(fp, &next) || !rd(fp + 8, &ret)) break;
        if (ret == 0) break;
        ret_out[n++] = ret;
        if (next <= fp || next - fp > PANIC_MAX_FRAME_JUMP) break;   /* la pila crece hacia abajo */
        fp = next;
    }
    return n;
}

void panic_hex64(uint64_t v);
static void ph64(uint64_t v)
{
    kprint("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t d = (uint8_t)((v >> shift) & 0x0F);
        kputc(d < 10 ? (char)('0' + d) : (char)('A' + d - 10));
    }
}

void panic_hex64(uint64_t v) { ph64(v); }

static int in_text(uint64_t a)
{
    return a >= PANIC_TEXT_LO && a < PANIC_TEXT_HI;
}

static void print_addr_line(const char *tag, uint64_t a)
{
    kprint(tag);
    ph64(a);
    if (!in_text(a)) kprint("  [fuera de .text]");
    kprint("\n");
}

void panic_print_context_with(panic_read64_fn rd, uint64_t pc, uint64_t lr,
                              uint64_t fp, uint64_t sp)
{
    uint64_t frames[PANIC_MAX_FRAMES];
    struct tcb *t = thread_current();
    int n;

    kprint("\nBACKTRACE (hilo ");
    if (t) {
        kprint_dec(t->tid);
        kprint(" '");
        kprint(t->name);
        kprint("'");
    } else {
        kprint("ninguno: arranque temprano");
    }
    kprint("):\n");
    print_addr_line("  #0  ", pc);
    if (lr) print_addr_line("  LR  ", lr);
    n = panic_walk_frames(fp, sp, rd, frames, PANIC_MAX_FRAMES);
    for (int i = 0; i < n; ++i) {
        kprint("  #");
        kprint_dec((uint32_t)(i + 1));
        kprint(i + 1 < 10 ? "  " : " ");
        ph64(frames[i]);
        if (!in_text(frames[i])) kprint("  [fuera de .text]");
        kprint("\n");
    }
    if (n == 0) kprint("  (cadena de frames no disponible)\n");
    kprint("  Resolver: python3 tools/symbolize.py <log> build/<arq>/soma.elf\n");
}

static int mem_read64(uint64_t addr, uint64_t *out)
{
    if ((addr & 7) != 0) return 0;
    if (vmm_virt_to_phys(addr) == 0 || vmm_virt_to_phys(addr + 7) == 0) return 0;
    *out = *(volatile uint64_t *)(uintptr_t)addr;
    return 1;
}

void panic_print_context(uint64_t pc, uint64_t lr, uint64_t fp, uint64_t sp)
{
    panic_print_context_with(mem_read64, pc, lr, fp, sp);
}

/* ------------------------------------------------------------------ */
/* Vigilante                                                           */
/* ------------------------------------------------------------------ */

static volatile uint64_t wd_last_beat = 0;
static volatile uint32_t wd_beats = 0;
static struct wd_state   wd_st;

int wd_evaluate(struct wd_state *st, uint64_t now_ms, uint64_t last_beat_ms, uint32_t limit_ms)
{
    if (last_beat_ms != st->last_beat_seen) {          /* hubo latido: todo en orden */
        st->last_beat_seen = last_beat_ms;
        st->reports = 0;
        return 0;
    }
    if (now_ms < last_beat_ms || now_ms - last_beat_ms < limit_ms) return 0;
    if (st->reports != 0 && now_ms - st->last_report < limit_ms) return 0;
    st->reports++;
    st->trips++;
    st->last_report = now_ms;
    return 1;
}

void watchdog_beat(uint64_t now_ms)
{
    wd_last_beat = now_ms;
    wd_beats = 1;
}

uint64_t watchdog_last_beat_ms(void) { return wd_last_beat; }
uint32_t watchdog_trips(void) { return wd_st.trips; }

void watchdog_tick(uint64_t now_ms, uint64_t pc, uint64_t fp, uint64_t sp)
{
    if (!wd_beats) return;                              /* aun no hay planificador */
    if (!wd_evaluate(&wd_st, now_ms, wd_last_beat, WATCHDOG_LIMIT_MS)) return;

    kprint("\n[WATCHDOG] Ningun hilo cedio la CPU en ");
    kprint_dec((uint32_t)((now_ms - wd_last_beat) / 1000u));
    kprint(" s (aviso ");
    kprint_dec(wd_st.reports);
    kprint("). El sistema puede estar colgado.");
    panic_print_context(pc, 0, fp, sp);
}
