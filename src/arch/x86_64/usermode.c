/* SOMA_PARCHE_014_RING3: modo usuario (ring 3) en x86_64. Ver usermode.h. */
#include <stdint.h>
#include <stddef.h>
#include "usermode.h"
#include "uaccess.h"
#include "vmm.h"
#include "pmm.h"
#include "idt.h"
#include "console.h"
#include "thread.h"
#include "elf.h"
#include "fs.h"
#include "bigfs_glue.h"
#include "audio_sys.h"
#include "fb.h"
#include "mem.h"

#define MSR_EFER   0xC0000080u
#define MSR_STAR   0xC0000081u
#define MSR_LSTAR  0xC0000082u
#define MSR_SFMASK 0xC0000084u

#define SEL_KCODE 0x08
#define SEL_UDATA 0x1B   /* 0x18 | RPL 3 */
#define SEL_UCODE 0x23   /* 0x20 | RPL 3 */

extern void tss_set_rsp0(uint64_t rsp0);
static int inited;

#define USER_MAX_UPAGES (USER_CODE_MAX_PAGES + USER_STACK_PAGES + 16)
struct upage { uint64_t va, frame, flags; };
struct ufd { uint32_t ino; uint64_t pos; int used; };

/* Un proceso de usuario. Los tres primeros campos los usa el ensamblador (desplazamientos 0, 8, 16). */
struct uproc {
    uint64_t user_rsp;       /*  0: rsp de usuario durante una llamada al sistema */
    uint64_t kstack;         /*  8: pila de kernel para SYSCALL/IRQ (bajo los marcos de entrada del hilo) */
    uint64_t saved_rsp;      /* 16: pila del hilo al entrar en ring 3 (para volver con usermode_leave) */
    struct upage pages[USER_MAX_UPAGES];   /* paginas de usuario del proceso (codigo, datos, pila) */
    int      npages;
    uint64_t entry, stack_top;
    uint64_t as;             /* PML4 propia (direccion fisica) */
    uint64_t deadline_ms;
    uint64_t start_ms, end_ms;
    int64_t  exit_code;
    uint32_t tid;
    int      state;          /* 0 libre, 1 vivo, 2 terminado (hilo ya recogido; falta `usermode_wait`) */
    int      finished;       /* 1 = salio por exit/fallo/plazo (distingue de 'matado desde fuera') */
    char     name[16];
    struct ufd fds[USER_MAX_FDS];           /* SOMA_PARCHE_019_FILES */
    char     args[USER_ARGS_MAX];
    int      audio;                         /* SOMA_PARCHE_020_AUDIO: este proceso tiene el flujo de audio abierto */
    int      fb;                            /* SOMA_PARCHE_021_FPU: este proceso tiene la pantalla */
    uint8_t  fpu[512] __attribute__((aligned(16)));   /* SOMA_PARCHE_021_FPU: estado x87/MMX/SSE (FXSAVE) del proceso */
    uint8_t  io[16384];                     /* bufer de rebote por proceso para SYS_READ (una lectura puede ceder la CPU) */
};

static struct uproc procs[USER_MAX_PROCS];
struct uproc *usermode_cur;           /* proceso en ejecucion (0 = hilo de kernel); lo lee el ensamblador */
static uint64_t slice_start_ms;
#define USER_SLICE_MS 10
#define USER_KSTACK_RESERVE 1024      /* se reserva arriba de la pila del hilo para sus marcos de entrada */
#define USER_EXIT_KILLED (-4000)

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi; __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

extern void usermode_syscall_entry(void);
extern int64_t usermode_enter(struct uproc *up, uint64_t rip, uint64_t rsp);   /* vuelve con el codigo de salida */
extern void usermode_leave(int64_t code) __attribute__((noreturn));

__asm__(
".text\n"
".global usermode_enter\n"
"usermode_enter:\n"
"    pushfq\n"
"    pushq %rbx\n pushq %rbp\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
"    movq %rsp, 16(%rdi)\n"
"    cli\n"
"    movw $0x1B, %ax\n movw %ax, %ds\n movw %ax, %es\n"
"    pushq $0x1B\n"            /* SS */
"    pushq %rdx\n"             /* RSP de usuario */
"    pushq $0x202\n"           /* RFLAGS: IF=1 */
"    pushq $0x23\n"            /* CS */
"    pushq %rsi\n"             /* RIP */
"    iretq\n"
".global usermode_leave\n"
"usermode_leave:\n"
"    movq usermode_cur(%rip), %r10\n"
"    movq %rdi, %rax\n"
"    movq 16(%r10), %rsp\n"
"    movw $0x10, %cx\n movw %cx, %ds\n movw %cx, %es\n"
"    popq %r15\n popq %r14\n popq %r13\n popq %r12\n popq %rbp\n popq %rbx\n"
"    popfq\n"
"    ret\n"
".global usermode_syscall_entry\n"
"usermode_syscall_entry:\n"
"    movq usermode_cur(%rip), %r10\n"
"    movq %rsp, 0(%r10)\n"
"    movq 8(%r10), %rsp\n"
"    pushq 0(%r10)\n"
"    pushq %rcx\n"             /* RIP de retorno */
"    pushq %r11\n"             /* RFLAGS */
"    subq $8, %rsp\n"          /* alineacion a 16 */
"    movq %rdx, %rcx\n"
"    movq %rsi, %rdx\n"
"    movq %rdi, %rsi\n"
"    movq %rax, %rdi\n"
"    cld\n"
"    call usermode_syscall\n"
"    addq $8, %rsp\n"
"    popq %r11\n"
"    popq %rcx\n"
"    popq %rsp\n"
"    sysretq\n"
);

static int smap_on, smep_on;

static inline void cpuid_leaf(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c2, uint32_t *d)
{
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c2), "=d"(*d) : "0"(leaf), "2"(sub));
}

int usermode_cpu_features(void) { return (smep_on ? 1 : 0) | (smap_on ? 2 : 0); }

/* SOMA_PARCHE_017_AS: SMEP (el kernel no ejecuta paginas de usuario) y SMAP (no las lee/escribe salvo
 * con stac/clac). Solo si la CPU los tiene (QEMU: -cpu qemu64,+smep,+smap). */
static void enable_smep_smap(void)
{
    uint32_t a, b, c2, d;
    cpuid_leaf(0, 0, &a, &b, &c2, &d);
    if (a < 7) return;
    cpuid_leaf(7, 0, &a, &b, &c2, &d);
    uint64_t cr4; __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if (b & (1u << 7))  { cr4 |= (1ULL << 20); smep_on = 1; }
    if (b & (1u << 20)) { cr4 |= (1ULL << 21); smap_on = 1; }
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
}

static void usermode_init(void)
{
    if (inited) return;
    enable_smep_smap();
    kprint("USERMODE: SMEP "); kprint(smep_on ? "activo" : "no disponible");
    kprint(", SMAP "); kprint(smap_on ? "activo" : "no disponible"); kprint(" (CPU)\n");
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1ULL);                       /* SCE */
    wrmsr(MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)SEL_KCODE << 32));
    wrmsr(MSR_LSTAR, (uint64_t)&usermode_syscall_entry);
    wrmsr(MSR_SFMASK, 0x700);                                      /* IF, TF, DF a 0 al entrar */
    inited = 1;
}

/* El rango [p, p+len) debe estar en la ventana de usuario y mapeado con el bit U (y, si need_write, escribible)
 * en cada pagina. len hasta 1 MiB. */
_Static_assert(UACC_PRESENT == VMM_FLAG_PRESENT && UACC_WRITABLE == VMM_FLAG_WRITABLE && UACC_USER == VMM_FLAG_USER, "uaccess.h desincronizado de vmm.h");
static uint64_t user_pte_of(void *ctx, uint64_t va) { return vmm_as_get_pte(((struct uproc *)ctx)->as, va); }
static int user_range(uint64_t p, uint64_t len, int need_write)
{
    if (!usermode_cur) return 0;
    return uaccess_range_ok(p, len, need_write, USER_BASE, USER_WINDOW, user_pte_of, usermode_cur);   /* PARCHE 102 */
}
static int user_range_ok(uint64_t p, uint64_t len) { return user_range(p, len, 0); }

/* SOMA_PARCHE_019_FILES: copias entre kernel y usuario con SMAP abierto solo durante la copia */
static void copy_from_user(void *dst, uint64_t usrc, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)usrc;
    if (smap_on) __asm__ volatile ("stac" ::: "memory");
    for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
    if (smap_on) __asm__ volatile ("clac" ::: "memory");
}
static void copy_to_user(uint64_t udst, const void *src, uint64_t n)
{
    uint8_t *d = (uint8_t *)udst; const uint8_t *s = (const uint8_t *)src;
    if (smap_on) __asm__ volatile ("stac" ::: "memory");
    for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
    if (smap_on) __asm__ volatile ("clac" ::: "memory");
}

#define E_FAULT  (-14)
#define E_BADF   (-9)
#define E_INVAL  (-22)
#define E_MFILE  (-24)

static struct ufd *get_fd(int64_t fd)
{
    if (fd < 0 || fd >= USER_MAX_FDS || !usermode_cur || !usermode_cur->fds[fd].used) return 0;
    return &usermode_cur->fds[fd];
}

static int64_t sys_open(uint64_t pptr, uint64_t plen)
{
    if (plen == 0 || plen > BIGFS_PATH_MAX || !user_range(pptr, plen, 0)) return E_FAULT;
    char path[BIGFS_PATH_MAX + 1];
    copy_from_user(path, pptr, plen); path[plen] = 0;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return e;
    uint32_t ino; int t = bigfs_lookup(fs, path, &ino);
    if (t < 0) return t;
    if (t != (int)BIGFS_T_FILE) return BIGFS_E_ISDIR;
    for (int i = 0; i < USER_MAX_FDS; ++i)
        if (!usermode_cur->fds[i].used) { usermode_cur->fds[i].used = 1; usermode_cur->fds[i].ino = ino; usermode_cur->fds[i].pos = 0; return i; }
    return E_MFILE;
}

static int64_t sys_read(uint64_t fdn, uint64_t ubuf, uint64_t len)
{
    struct ufd *f = get_fd((int64_t)fdn);
    if (!f) return E_BADF;
    if (len == 0) return 0;
    if (len > 65536) len = 65536;                               /* con IF=0: acota lo que se retrasa el reloj */
    if (!user_range(ubuf, len, 1)) return E_FAULT;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return e;
    uint64_t done = 0;
    while (done < len) {
        uint64_t n = len - done < sizeof(usermode_cur->io) ? len - done : sizeof(usermode_cur->io);
        int64_t g = bigfs_read(fs, f->ino, f->pos, usermode_cur->io, n);
        if (g < 0) return done ? (int64_t)done : g;
        if (g == 0) break;
        copy_to_user(ubuf + done, usermode_cur->io, (uint64_t)g);
        f->pos += (uint64_t)g; done += (uint64_t)g;
        if ((uint64_t)g < n) break;
    }
    return (int64_t)done;
}

static int64_t sys_seek(uint64_t fdn, int64_t off, uint64_t whence)
{
    struct ufd *f = get_fd((int64_t)fdn);
    if (!f) return E_BADF;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return e;
    struct bigfs_stat st;
    if (bigfs_stat(fs, f->ino, &st) < 0) return BIGFS_E_NOENT;
    int64_t base = whence == 0 ? 0 : whence == 1 ? (int64_t)f->pos : whence == 2 ? (int64_t)st.size : -1;
    if (base < 0) return E_INVAL;
    int64_t np;                                                  /* PARCHE 030: el desbordamiento con signo era indefinido */
    if (__builtin_add_overflow(base, off, &np) || np < 0) return E_INVAL;
    f->pos = (uint64_t)np;
    return np;
}

static int64_t sys_fsize(uint64_t fdn)
{
    struct ufd *f = get_fd((int64_t)fdn);
    if (!f) return E_BADF;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return e;
    struct bigfs_stat st;
    if (bigfs_stat(fs, f->ino, &st) < 0) return BIGFS_E_NOENT;
    return (int64_t)st.size;
}

/* SOMA_PARCHE_021_FPU: pantalla */
static int64_t sys_fb_blit(uint64_t ubuf, uint64_t wh, uint64_t xy)
{
    if (!usermode_cur->fb) return E_BADF;
    uint32_t w = (uint32_t)(wh >> 16) & 0xFFFF, h = (uint32_t)wh & 0xFFFF, x = (uint32_t)(xy >> 16) & 0xFFFF, y = (uint32_t)xy & 0xFFFF;
    uint32_t sw = fb_get_width(), sh = fb_get_height();
    if (w == 0 || h == 0 || x + w > sw || y + h > sh) return E_INVAL;
    uint64_t bytes = (uint64_t)w * h * 4;
    if (!user_range(ubuf, bytes, 0)) return E_FAULT;
    volatile uint32_t *dst = fb_get_buffer();
    if (!dst) return E_FAULT;
    /* se copia fila a fila con SMAP abierto solo durante la copia (con interrupciones cerradas: nadie mas toca el
     * backbuffer a mitad); despues se marca el rectangulo y se presenta */
    const uint8_t *src = (const uint8_t *)ubuf;
    if (smap_on) __asm__ volatile ("stac" ::: "memory");
    for (uint32_t r = 0; r < h; ++r) {
        uint32_t *d = (uint32_t *)(uintptr_t)(dst + (uint64_t)(y + r) * sw + x);
        const uint32_t *s = (const uint32_t *)(src + (uint64_t)r * w * 4);
        uint64_t n = w;
        __asm__ volatile ("rep movsl" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    }
    if (smap_on) __asm__ volatile ("clac" ::: "memory");
    fb_damage((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
    fb_present();
    return 0;
}

/* SOMA_PARCHE_020_AUDIO */
static int64_t sys_audio_write(uint64_t ubuf, uint64_t len)
{
    if (!usermode_cur->audio) return E_BADF;
    if (len == 0) return 0;
    if (len > 65536) len = 65536;
    len &= ~3ULL;
    if (len == 0 || !user_range(ubuf, len, 0)) return E_FAULT;
    uint64_t done = 0;
    while (done < len) {
        uint64_t n = len - done < sizeof(usermode_cur->io) ? len - done : sizeof(usermode_cur->io);
        copy_from_user(usermode_cur->io, ubuf + done, n);
        int k = audio_sys_write(usermode_cur->io, (uint32_t)n);
        if (k <= 0) break;
        done += (uint64_t)k;
        if ((uint64_t)k < n) break;
    }
    return (int64_t)done;
}

static int64_t sys_getargs(uint64_t ubuf, uint64_t len)
{
    if (len == 0 || !user_range(ubuf, len, 1)) return E_FAULT;
    uint64_t n = 0; while (usermode_cur->args[n] && n < USER_ARGS_MAX - 1) n++;
    uint64_t c = n + 1 < len ? n + 1 : len;                    /* con NUL si cabe */
    char tmp[USER_ARGS_MAX];
    for (uint64_t i = 0; i < c; ++i) tmp[i] = i < n ? usermode_cur->args[i] : 0;
    if (c == len) tmp[c - 1] = 0;
    copy_to_user(ubuf, tmp, c);
    return (int64_t)n;
}

int64_t usermode_syscall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2)
{
    (void)a2;
    switch (nr) {
    case SYS_EXIT:
        usermode_cur->exit_code = (int64_t)a0;
        usermode_leave((int64_t)a0);
    case SYS_WRITE:
        if (a1 == 0) return 0;                                   /* PARCHE 102: escribir 0 bytes no es un fallo */
        if (!user_range_ok(a0, a1)) return -1;
        {   /* se copia a un buffer del kernel con SMAP abierto el minimo tiempo, y se imprime ya cerrado */
            char buf[256];
            const char *s = (const char *)a0;
            for (uint64_t off = 0; off < a1; off += sizeof(buf)) {
                uint64_t n = a1 - off < sizeof(buf) ? a1 - off : sizeof(buf);
                if (smap_on) __asm__ volatile ("stac" ::: "memory");
                for (uint64_t i = 0; i < n; ++i) buf[i] = s[off + i];
                if (smap_on) __asm__ volatile ("clac" ::: "memory");
                for (uint64_t i = 0; i < n; ++i) kputc(buf[i]);
            }
        }
        return (int64_t)a1;
    case SYS_YIELD:
        thread_yield();
        return 0;
    case SYS_SLEEP:
        thread_sleep((uint32_t)(a0 > 10000 ? 10000 : a0));
        return 0;
    case SYS_GETPID:
        return (int64_t)usermode_cur->tid;
    case SYS_OPEN:    return sys_open(a0, a1);
    case SYS_READ:    return sys_read(a0, a1, a2);
    case SYS_SEEK:    return sys_seek(a0, (int64_t)a1, a2);
    case SYS_CLOSE:   { struct ufd *f = get_fd((int64_t)a0); if (!f) return E_BADF; f->used = 0; return 0; }
    case SYS_FSIZE:   return sys_fsize(a0);
    case SYS_GETARGS: return sys_getargs(a0, a1);
    case SYS_AUDIO_OPEN: {
        int r = audio_sys_open((uint32_t)a0, usermode_cur->tid);
        if (r == 0) usermode_cur->audio = 1;
        return r;
    }
    case SYS_AUDIO_WRITE: return sys_audio_write(a0, a1);
    case SYS_AUDIO_STATUS: {
        if (!usermode_cur->audio) return E_BADF;
        if (!user_range(a0, 24, 1)) return E_FAULT;
        uint64_t st[3]; int r = audio_sys_status(st);
        if (r) return r;
        copy_to_user(a0, st, 24);
        return 0;
    }
    case SYS_UPTIME_MS: return (int64_t)timer_get_uptime_ms();
    case SYS_FB_OPEN: {
        if (usermode_cur->fb) return -16;
        int r = fb_user_begin((uint32_t)a0, (uint32_t)a1);
        if (r == 0) usermode_cur->fb = 1;
        return r;
    }
    case SYS_FB_INFO: {
        if (!usermode_cur->fb) return E_BADF;
        if (!user_range(a0, 12, 1)) return E_FAULT;
        uint32_t info[3] = { fb_get_width(), fb_get_height(), fb_get_width() * 4 };
        copy_to_user(a0, info, 12);
        return 0;
    }
    case SYS_FB_BLIT: return sys_fb_blit(a0, a1, a2);
    case SYS_FB_CLOSE:
        if (!usermode_cur->fb) return E_BADF;
        fb_user_end(); usermode_cur->fb = 0;
        return 0;
    case SYS_AUDIO_CLOSE:
        if (!usermode_cur->audio) return E_BADF;
        audio_sys_close(a0 ? 1 : 0);
        usermode_cur->audio = 0;
        return 0;
    default:
        return -38;   /* ENOSYS */
    }
}

int usermode_active(void) { return usermode_cur != 0; }

void usermode_on_fault(uint64_t vector, uint64_t rip, uint64_t cr2)
{
    kprint("[USER] proceso terminado por excepcion de CPU: vector ");
    kprint_dec((uint32_t)vector);
    kprint(" rip=0x"); kprint_hex32((uint32_t)(rip >> 32)); kprint_hex32((uint32_t)rip);
    if (vector == 14) { kprint(" cr2=0x"); kprint_hex32((uint32_t)(cr2 >> 32)); kprint_hex32((uint32_t)cr2); }
    kprint("\n");
    usermode_cur->exit_code = USER_EXIT_FAULT_BASE - (int64_t)vector;
    usermode_leave(USER_EXIT_FAULT_BASE - (int64_t)vector);
}

/* IRQ de reloj. Solo se actua si se interrumpio ring 3: ahi el kernel no tiene cerrojos ni estructuras a medias,
 * asi que ceder la CPU es seguro (el resto del kernel sigue siendo cooperativo). */
void usermode_on_tick(uint64_t now_ms, uint64_t cs)
{
    struct uproc *up = usermode_cur;
    if (!up || (cs & 3) != 3) return;
    if (now_ms >= up->deadline_ms) {
        kprint("[USER] proceso terminado: supero el limite de tiempo\n");
        up->exit_code = USER_EXIT_TIMEOUT;
        usermode_leave(USER_EXIT_TIMEOUT);
    }
    if (now_ms - slice_start_ms >= USER_SLICE_MS) thread_yield();
}

/* ---- SOMA_PARCHE_017_AS: cada proceso tiene su CR3; los hilos de kernel corren siempre con el del kernel ---- */
void arch_thread_switch(struct tcb *prev, struct tcb *next)
{
    /* SOMA_PARCHE_021_FPU: el kernel no usa SSE/x87 (-mgeneral-regs-only) y las interrupciones tampoco, asi que solo
     * los procesos de usuario tienen estado que proteger. Se guarda al salir y se restaura al entrar (sin
     * conmutacion perezosa: es mas simple y los procesos son pocos). */
    struct uproc *pu = prev ? (struct uproc *)prev->uproc : 0;
    if (pu) __asm__ volatile ("fxsave64 %0" : "=m"(*(uint8_t (*)[512])pu->fpu) : : "memory");
    struct uproc *nu = next ? (struct uproc *)next->uproc : 0;
    if (nu) __asm__ volatile ("fxrstor64 %0" : : "m"(*(uint8_t (*)[512])nu->fpu) : "memory");
    if (nu) {
        tss_set_rsp0(nu->kstack);
        vmm_load_as(nu->as);
    } else {
        vmm_load_as(vmm_kernel_as());
    }
    usermode_cur = nu;
    slice_start_ms = timer_get_uptime_ms();
}

static void free_pages(struct uproc *up)
{
    for (int i = 0; i < up->npages; ++i) if (up->pages[i].frame) { pmm_free_frame(up->pages[i].frame); up->pages[i].frame = 0; }
    up->npages = 0;
}

void arch_thread_cleanup(struct tcb *t)
{
    struct uproc *up = (struct uproc *)t->uproc;
    if (!up || up->state == 0 || up->tid != t->tid) return;    /* ranura ya reutilizada: no es de este hilo */
    if (!up->finished) up->exit_code = USER_EXIT_KILLED;       /* matado desde fuera (thread_kill) */
    if (up->audio) { audio_sys_owner_died(up->tid); up->audio = 0; }
    if (up->fb) { fb_user_end(); up->fb = 0; }
    free_pages(up);
    vmm_as_destroy(up->as); up->as = 0;                        /* nunca esta cargado: este hilo ya no es el actual */
    up->end_ms = timer_get_uptime_ms();
    up->state = 2;
}

static void proc_thread_main(void *arg)
{
    struct uproc *up = (struct uproc *)arg;
    up->start_ms = timer_get_uptime_ms();
    (void)usermode_enter(up, up->entry, up->stack_top);
    up->finished = 1;
    if (up->audio) { audio_sys_close(0); up->audio = 0; }       /* salio con el audio abierto (exit, fallo o plazo) */
    if (up->fb) { fb_user_end(); up->fb = 0; }                   /* y con la pantalla */
    /* vuelve aqui al terminar (exit, fallo o plazo); el codigo ya esta en up->exit_code */
}

/* ---- construccion de procesos: las paginas se reservan directamente en la ranura (un programa de hasta 4 MiB
 * son 1024 paginas: no caben listas temporales en la pila del kernel) ---- */
static struct uproc *slot_begin(void)
{
    for (int i = 0; i < USER_MAX_PROCS; ++i)
        if (procs[i].state == 0) {
            struct uproc *up = &procs[i];
            up->state = 3;                                   /* reservada, construyendose */
            up->npages = 0; up->as = 0; up->tid = 0; up->exit_code = 0; up->finished = 0;
            up->start_ms = up->end_ms = 0; up->user_rsp = 0; up->saved_rsp = 0;
            for (int k = 0; k < USER_MAX_FDS; ++k) up->fds[k].used = 0;
            up->args[0] = 0; up->name[0] = 0; up->audio = 0; up->fb = 0;
            /* estado inicial de la FPU: FCW=0x037F (todas las excepciones enmascaradas, doble extendida), MXCSR=0x1F80
             * (idem SSE, redondeo al mas cercano), registros vacios: igual que tras FNINIT */
            for (int k = 0; k < 512; ++k) up->fpu[k] = 0;
            up->fpu[0] = 0x7F; up->fpu[1] = 0x03;
            up->fpu[24] = 0x80; up->fpu[25] = 0x1F;
            return up;
        }
    return 0;
}

static void free_pages(struct uproc *up);
static void slot_abort(struct uproc *up)
{
    free_pages(up);
    if (up->as) { vmm_as_destroy(up->as); up->as = 0; }
    up->state = 0;
}

static int add_page(struct uproc *up, uint64_t va, uint64_t flags, uint8_t **page_out)
{
    if (up->npages >= USER_MAX_UPAGES) return -1;
    uintptr_t f = pmm_alloc_frame();
    if (!f) return -1;
    uint8_t *p = (uint8_t *)f;
    for (size_t k = 0; k < 4096; ++k) p[k] = 0;
    up->pages[up->npages].va = va; up->pages[up->npages].frame = f; up->pages[up->npages].flags = flags;
    up->npages++;
    if (page_out) *page_out = p;
    return 0;
}

static int add_stack(struct uproc *up)
{
    for (int i = 0; i < USER_STACK_PAGES; ++i)
        if (add_page(up, USER_STACK_VA + (uint64_t)i * 4096, VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITABLE | VMM_FLAG_NX, 0) != 0) return -1;
    return 0;
}

/* Crea la PML4, mapea las paginas ya reservadas y lanza el hilo. Devuelve el indice o <0 (y libera todo). */
static int slot_launch(struct uproc *up, const char *name, uint64_t entry, uint32_t max_ms, const char *args)
{
    int idx = (int)(up - procs);
    up->as = vmm_as_create();
    int ok = up->as != 0;
    for (int i = 0; i < up->npages && ok; ++i)
        if (vmm_as_map(up->as, up->pages[i].va, up->pages[i].frame, up->pages[i].flags) != 0) ok = 0;
    if (!ok) { slot_abort(up); return -5; }

    up->entry = entry; up->stack_top = USER_STACK_VA + (uint64_t)USER_STACK_PAGES * 4096;
    up->deadline_ms = timer_get_uptime_ms() + max_ms;
    up->name[0] = 0;
    for (int i = 0; i < 15 && name && name[i]; ++i) { up->name[i] = name[i]; up->name[i + 1] = 0; }
    up->args[0] = 0;
    for (int i = 0; i < USER_ARGS_MAX - 1 && args && args[i]; ++i) { up->args[i] = args[i]; up->args[i + 1] = 0; }

    struct tcb *t = thread_create(name ? name : "uproc", proc_thread_main, up);
    if (!t) { slot_abort(up); return -4; }
    up->tid = t->tid;
    up->kstack = (t->kstack_top - USER_KSTACK_RESERVE) & ~0xFULL;
    up->state = 1;
    t->uproc = up;           /* antes de que corra por primera vez: el planificador es cooperativo */
    return idx;
}

int usermode_spawn(const char *name, const void *code, size_t len, uint32_t max_ms)
{
    if (!code || len == 0 || len > 4096) return -1;
    usermode_init();
    struct uproc *up = slot_begin();
    if (!up) return -2;
    uint8_t *pg;
    if (add_page(up, USER_CODE_VA, VMM_FLAG_PRESENT | VMM_FLAG_USER, &pg) != 0) { slot_abort(up); return -3; }   /* R+X */
    for (size_t i = 0; i < len; ++i) pg[i] = ((const uint8_t *)code)[i];
    if (add_stack(up) != 0) { slot_abort(up); return -3; }
    return slot_launch(up, name, USER_CODE_VA, max_ms, 0);
}

/* Carga un ejecutable ELF validado. *err recibe el codigo de elf_parse (o 0). */
int usermode_exec(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, int *err)
{
    static const struct elf_limits lim = { USER_BASE, USER_CODE_AREA, USER_CODE_MAX_PAGES };
    struct elf_image im;
    int r = elf_parse((const uint8_t *)img, len, &lim, &im);
    if (err) *err = r;
    if (r != ELF_OK) return -10;
    usermode_init();

    struct uproc *up = slot_begin();
    if (!up) return -2;
    for (int si = 0; si < im.nsegs; ++si) {
        const struct elf_seg *s = &im.seg[si];
        uint64_t first = s->vaddr & ~0xFFFULL, last = (s->vaddr + s->memsz - 1) & ~0xFFFULL;
        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (s->flags & ELF_PF_W) flags |= VMM_FLAG_WRITABLE;
        if (!(s->flags & ELF_PF_X)) flags |= VMM_FLAG_NX;
        for (uint64_t va = first; va <= last; va += 4096) {
            uint8_t *dst;
            if (add_page(up, va, flags, &dst) != 0) { slot_abort(up); return -3; }
            /* bytes del archivo que caen en esta pagina */
            uint64_t lo = va > s->vaddr ? va : s->vaddr;                        /* primer byte de la pagina dentro del segmento */
            uint64_t hi = va + 4096 < s->vaddr + s->filesz ? va + 4096 : s->vaddr + s->filesz;   /* fin dentro del archivo */
            for (uint64_t addr = lo; addr < hi; ++addr)
                dst[addr - va] = ((const uint8_t *)img)[s->offset + (addr - s->vaddr)];
        }
    }
    if (add_stack(up) != 0) { slot_abort(up); return -3; }
    return slot_launch(up, name, im.entry, max_ms, args);
}

int64_t usermode_wait(int idx)
{
    if (idx < 0 || idx >= USER_MAX_PROCS || procs[idx].state == 0) return -3000;
    struct uproc *up = &procs[idx];
    while (up->state != 2) thread_sleep(1);
    int64_t code = up->exit_code;
    up->state = 0;
    return code;
}

int64_t usermode_run_code(const void *code, size_t len, uint32_t max_ms)
{
    int idx = usermode_spawn("uproc", code, len, max_ms);
    if (idx < 0) return -3000 + idx;
    return usermode_wait(idx);
}

/* Lista de procesos (tambien libera los terminados en segundo plano). */
int usermode_format_table(char *out, uint32_t max, int reap)
{
    uint32_t pos = 0;
    #define PUTS(str) do { const char *_s = (str); while (*_s && pos + 1 < max) out[pos++] = *_s++; } while (0)
    #define PUTN(v) do { char _b[24]; int _i = 23; uint64_t _v = (uint64_t)(v); _b[_i] = 0; if (!_v) _b[--_i] = '0'; while (_v) { _b[--_i] = (char)('0' + _v % 10); _v /= 10; } PUTS(_b + _i); } while (0)
    int shown = 0;
    PUTS("PROCESOS DE USUARIO\n IDX  TID  Nombre           Estado      Codigo   ms\n");
    for (int i = 0; i < USER_MAX_PROCS; ++i) {
        struct uproc *up = &procs[i];
        if (up->state == 0 || up->state == 3) continue;
        shown++;
        PUTS(" "); PUTN(i); PUTS("    "); PUTN(up->tid); PUTS("    "); PUTS(up->name); PUTS("          ");
        if (up->state == 1) { PUTS("vivo       -"); PUTS("        "); PUTN(timer_get_uptime_ms() - up->start_ms); }
        else {
            PUTS("terminado  ");
            if (up->exit_code < 0) { PUTS("-"); PUTN(-up->exit_code); } else PUTN(up->exit_code);
            PUTS("      "); PUTN(up->end_ms - up->start_ms);
            if (reap) up->state = 0;
        }
        PUTS("\n");
    }
    if (!shown) PUTS(" (ninguno)\n");
    out[pos < max ? pos : max - 1] = 0;
    #undef PUTS
    #undef PUTN
    return (int)pos;
}

/* Los programas ELF que lleva el kernel (compilados desde src/user/) se copian a /bin/ en cada arranque. */
extern const uint8_t _binary_hello_elf_start[], _binary_hello_elf_end[];
extern const uint8_t _binary_fault_elf_start[], _binary_fault_elf_end[];
extern const uint8_t _binary_ticker_elf_start[], _binary_ticker_elf_end[];
extern const uint8_t _binary_sumfile_elf_start[], _binary_sumfile_elf_end[];
extern const uint8_t _binary_wavplay_elf_start[], _binary_wavplay_elf_end[];
extern const uint8_t _binary_ssetest_elf_start[], _binary_ssetest_elf_end[];
extern const uint8_t _binary_fbtest_elf_start[], _binary_fbtest_elf_end[];
extern const uint8_t _binary_sysfuzz_elf_start[], _binary_sysfuzz_elf_end[];

void usermode_install_programs(void)
{
    static const struct { const char *path; const uint8_t *a, *b; } progs[] = {
        { "/bin/hello.elf",  _binary_hello_elf_start,  _binary_hello_elf_end },
        { "/bin/fault.elf",  _binary_fault_elf_start,  _binary_fault_elf_end },
        { "/bin/ticker.elf", _binary_ticker_elf_start, _binary_ticker_elf_end },
        { "/bin/sumfile.elf", _binary_sumfile_elf_start, _binary_sumfile_elf_end },
        { "/bin/wavplay.elf", _binary_wavplay_elf_start, _binary_wavplay_elf_end },
        { "/bin/ssetest.elf", _binary_ssetest_elf_start, _binary_ssetest_elf_end },
        { "/bin/fbtest.elf", _binary_fbtest_elf_start, _binary_fbtest_elf_end },
        { "/bin/sysfuzz.elf", _binary_sysfuzz_elf_start, _binary_sysfuzz_elf_end },
    };
    for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); ++i)
        vfs_write(progs[i].path, (const char *)progs[i].a, (uint32_t)(progs[i].b - progs[i].a));
}

/* ------------------------------------------------------------------ programas de prueba */
__asm__(
".section .rodata\n"
".global uprog_ok_start\n.global uprog_ok_end\n"
"uprog_ok_start:\n"
"    movq $1, %rax\n movq $0x100000, %rdi\n movq $4, %rsi\n syscall\n"     /* write con puntero del kernel */
"    movq %rax, %r12\n"
"    leaq 2f(%rip), %rdi\n movq $(3f-2f), %rsi\n movq $1, %rax\n syscall\n"  /* write valido */
"    movw %cs, %ax\n andq $3, %rax\n"                                         /* CPL */
"    leaq 40(%rax), %rdi\n"                                                   /* 40 + CPL */
"    cmpq $-1, %r12\n je 1f\n movq $1, %rdi\n"
"1:  xorq %rax, %rax\n syscall\n"
"    jmp .\n"
"2:  .ascii \"[USER] hola desde ring 3 (CPL=3)\\n\"\n"
"3:\n"
"uprog_ok_end:\n"
".global uprog_pf_start\n.global uprog_pf_end\n"
"uprog_pf_start:\n movq $0x100000, %rax\n movq (%rax), %rbx\n jmp .\nuprog_pf_end:\n"
".global uprog_gp_start\n.global uprog_gp_end\n"
"uprog_gp_start:\n hlt\n jmp .\nuprog_gp_end:\n"
".global uprog_loop_start\n.global uprog_loop_end\n"
"uprog_loop_start:\n jmp .\nuprog_loop_end:\n"
".global uprog_b_start\n.global uprog_b_end\n"
"uprog_b_start:\n"
"    leaq 2f(%rip), %rdi\n movq $(3f-2f), %rsi\n movq $1, %rax\n syscall\n"
"    movq $30, %rdi\n movq $3, %rax\n syscall\n"                  /* sleep(30 ms): solo despierta si el reloj quita la CPU al que gira */
"    movq $7, %rdi\n xorq %rax, %rax\n syscall\n"
"    jmp .\n"
"2:  .ascii \"[USER] B: vivo mientras otro proceso gira\\n\"\n"
"3:\n"
"uprog_b_end:\n"
/* aislamiento: ambos usan la MISMA direccion virtual de pila; cada uno escribe su valor, duerme y lo relee */
".macro ISO name, val\n"
".global uprog_\\name\\()_start\n.global uprog_\\name\\()_end\n"
"uprog_\\name\\()_start:\n"
"    movabsq $0x8001000000, %rbx\n movq $\\val, (%rbx)\n"
"    movq $60, %rdi\n movq $3, %rax\n syscall\n"
"    movq (%rbx), %rdi\n xorq %rax, %rax\n syscall\n"
"    jmp .\n"
"uprog_\\name\\()_end:\n"
".endm\n"
"ISO isoa, 0xAA\n"
"ISO isob, 0xBB\n"
".text\n"
);
extern const uint8_t uprog_b_start[], uprog_b_end[], uprog_isoa_start[], uprog_isoa_end[],
                     uprog_isob_start[], uprog_isob_end[];
extern const uint8_t uprog_ok_start[], uprog_ok_end[], uprog_pf_start[], uprog_pf_end[],
                     uprog_gp_start[], uprog_gp_end[], uprog_loop_start[], uprog_loop_end[];

static int expect(const char *name, int64_t got, int64_t want)
{
    kprint("  "); kprint(name); kprint(": ");
    if (got == want) { kprint("OK\n"); return 0; }
    kprint("FALLO (codigo "); kprint_dec((uint32_t)(got < 0 ? -got : got));
    kprint(got < 0 ? " negativo" : ""); kprint(", esperado ");
    kprint_dec((uint32_t)(want < 0 ? -want : want)); kprint(want < 0 ? " negativo)\n" : ")\n");
    return 1;
}

int usermode_selftest(void)
{
    int bad = 0;
    kprint("USERMODE: prueba de ring 3\n");
    bad += expect("syscalls + CPL=3 + puntero de kernel rechazado",
                  usermode_run_code(uprog_ok_start, (size_t)(uprog_ok_end - uprog_ok_start), 500), 43);
    bad += expect("lectura de memoria del kernel -> #PF, proceso muerto, kernel vivo",
                  usermode_run_code(uprog_pf_start, (size_t)(uprog_pf_end - uprog_pf_start), 500),
                  USER_EXIT_FAULT_BASE - 14);
    bad += expect("instruccion privilegiada (hlt) -> #GP, proceso muerto, kernel vivo",
                  usermode_run_code(uprog_gp_start, (size_t)(uprog_gp_end - uprog_gp_start), 500),
                  USER_EXIT_FAULT_BASE - 13);
    bad += expect("bucle infinito -> cortado por el plazo",
                  usermode_run_code(uprog_loop_start, (size_t)(uprog_loop_end - uprog_loop_start), 200),
                  USER_EXIT_TIMEOUT);

    /* ---- concurrencia (parche 015) ---- */
    #define LEN(n) ((size_t)(uprog_##n##_end - uprog_##n##_start))
    {   /* A gira sin ceder; B debe ejecutarse igualmente (preemption en ring 3) y terminar antes que A */
        int a = usermode_spawn("spin", uprog_loop_start, LEN(loop), 400);
        int b = usermode_spawn("B", uprog_b_start, LEN(b), 1000);
        uint64_t t0 = timer_get_uptime_ms();
        int64_t rb = usermode_wait(b);
        uint64_t tb = timer_get_uptime_ms() - t0;
        int64_t ra = usermode_wait(a);
        bad += expect("preemption: B termina (7) mientras A gira sin ceder", rb, 7);
        bad += expect("preemption: A cortado por plazo", ra, USER_EXIT_TIMEOUT);
        kprint("  B termino a los "); kprint_dec((uint32_t)tb); kprint(" ms (A gira hasta 400 ms)\n");
        if (tb > 250) { kprint("  FALLO: B tardo demasiado\n"); bad++; }
    }
    {   /* misma direccion virtual, memoria distinta */
        int a = usermode_spawn("isoA", uprog_isoa_start, LEN(isoa), 1000);
        int b = usermode_spawn("isoB", uprog_isob_start, LEN(isob), 1000);
        int64_t ra = usermode_wait(a), rb = usermode_wait(b);
        bad += expect("aislamiento: A relee su valor 0xAA (170)", ra, 0xAA);
        bad += expect("aislamiento: B relee su valor 0xBB (187)", rb, 0xBB);
    }
    {   /* un proceso muere por #PF mientras otro sigue vivo */
        int a = usermode_spawn("spin2", uprog_loop_start, LEN(loop), 300);
        int b = usermode_spawn("pf", uprog_pf_start, LEN(pf), 1000);
        int64_t rb = usermode_wait(b), ra = usermode_wait(a);
        bad += expect("fallo aislado: el que lee memoria del kernel muere (#PF)", rb, USER_EXIT_FAULT_BASE - 14);
        bad += expect("fallo aislado: el otro proceso sigue hasta su plazo", ra, USER_EXIT_TIMEOUT);
    }
    {   /* SOMA_PARCHE_017_AS: las paginas de usuario NO existen en el espacio de direcciones del kernel */
        int a = usermode_spawn("spin3", uprog_loop_start, LEN(loop), 300);
        thread_sleep(20);                           /* deja que arranque y quede cargado su CR3 al volver */
        bad += expect("aislamiento: PML4 del kernel sin entradas de usuario con un proceso vivo",
                      vmm_kernel_user_region_empty() ? 1 : 0, 1);
        bad += expect("aislamiento: el kernel no ve la pagina de codigo del proceso",
                      vmm_get_pte(USER_CODE_VA) == 0 ? 1 : 0, 1);
        (void)usermode_wait(a);
    }
    {   /* todo se libero: se puede repetir y no se pierden marcos */
        size_t f0, u0, t0m, f1, u1, t1m;
        pmm_get_stats(&f0, &u0, &t0m);
        int64_t r = usermode_run_code(uprog_ok_start, (size_t)(uprog_ok_end - uprog_ok_start), 500);
        thread_sleep(5);
        pmm_get_stats(&f1, &u1, &t1m);
        bad += expect("repetible tras liberar procesos", r, 43);
        kprint("  marcos libres antes/despues: "); kprint_dec((uint32_t)f0); kprint("/"); kprint_dec((uint32_t)f1); kprint("\n");
        if (f1 + 2 < f0) { kprint("  FALLO: fuga de marcos\n"); bad++; }
    }
    #undef LEN
    kprint(bad ? "USERMODE: FALLO\n" : "USERMODE: OK\n");
    return bad;
}

/* ================================================================== PARCHE 117: fuzzing de syscalls desde ring 3
 * Lanza `nproc` procesos hostiles (/bin/sysfuzz.elf) de 3 en 3 con semillas distintas. Cada uno es su propio oraculo (ver src/user/sysfuzz.c).
 * Despues comprueba que el kernel sigue integro: sin fugas de marcos, monton consistente, region de usuario vacia en la PML4 del kernel. */
static void fz_u32(char *d, uint32_t *n, uint32_t v) { char t[12]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) d[(*n)++] = t[--k]; }
int usermode_fuzz(int nproc, uint32_t iters, uint32_t seed)
{
    if (nproc < 1) nproc = 1;
    if (nproc > 500) nproc = 500;
    if (iters < 10) iters = 10;
    if (iters > 1000000) iters = 1000000;
    uint32_t ilen = 0; char *img = vfs_read_alloc("/bin/sysfuzz.elf", &ilen);
    if (!img || ilen == 0) { kprint("[USFUZZ] no encuentro /bin/sysfuzz.elf\n"); if (img) kfree(img); return -1; }
    /* La pasada principal (nivel 0) hace crecer cosas legitimas de un solo uso (cache del backbuffer, tablas de paginas del monton): su recuento de marcos es solo
     * informativo. La medida de fugas se hace en una segunda pasada corta (nivel 1) ya en estado estable: si cada proceso perdiera un marco, aqui se veria. */
    static int nivel;
    int nv = nivel;
    size_t f0 = 0, u0 = 0, t0 = 0, f1 = 0, u1 = 0, t1 = 0;
    struct kheap_counters h0, h1;
    kheap_get_counters(&h0);
    pmm_get_stats(&f0, &u0, &t0);
    kprint("[USFUZZ] "); kprint_dec((uint32_t)nproc); kprint(" procesos hostiles x "); kprint_dec(iters); kprint(" syscalls maliciosas (semilla "); kprint_dec(seed); kprint(")\n");
    int clean = 0, breach = 0, faulted = 0, timeout = 0, other = 0, norun = 0; uint64_t t_start = timer_get_uptime_ms();
    int idx[3];
    for (int base = 0; base < nproc; base += 3) {
        int w = nproc - base < 3 ? nproc - base : 3;
        for (int k = 0; k < w; ++k) {
            char args[40]; uint32_t n = 0; fz_u32(args, &n, seed + (uint32_t)(base + k) * 7919u); args[n++] = ' '; fz_u32(args, &n, iters); args[n] = 0;
            int err = 0; idx[k] = usermode_exec("sysfuzz", img, ilen, 20000, args, &err);
            if (idx[k] < 0) norun++;
        }
        for (int k = 0; k < w; ++k) {
            if (idx[k] < 0) continue;
            int64_t code = usermode_wait(idx[k]);
            if (code == 0) clean++;
            else if (code > 0 && code <= 100) { breach++; kprint("[USFUZZ] VIOLACION: el proceso "); kprint_dec((uint32_t)(base + k)); kprint(" detecto "); kprint_dec((uint32_t)code); kprint(" llamadas aceptadas indebidamente\n"); }
            else if (code <= USER_EXIT_FAULT_BASE && code > USER_EXIT_FAULT_BASE - 32) { faulted++; kprint("[USFUZZ] el proceso "); kprint_dec((uint32_t)(base + k)); kprint(" murio por excepcion (vector "); kprint_dec((uint32_t)(USER_EXIT_FAULT_BASE - code)); kprint(")\n"); }
            else if (code == USER_EXIT_TIMEOUT) { timeout++; kprint("[USFUZZ] el proceso "); kprint_dec((uint32_t)(base + k)); kprint(" no termino en su plazo\n"); }
            else { other++; kprint("[USFUZZ] codigo de salida inesperado "); kprint(code < 0 ? "-" : ""); kprint_dec((uint32_t)(code < 0 ? -code : code)); kprint(" en el proceso "); kprint_dec((uint32_t)(base + k)); kprint("\n"); }
        }
    }
    kfree(img);
    thread_sleep(20);
    pmm_get_stats(&f1, &u1, &t1);
    kheap_get_counters(&h1);
    /* fuga de MARCOS de procesos = marcos perdidos que NO se explican por el crecimiento del monton (el backbuffer de pantalla que pide SYS_FB_OPEN se conserva
     * como cache hasta 8 MiB y es crecimiento legitimo; 1 bloque vivo como mucho) */
    long perdidos = (long)f0 - (long)f1, crec = (long)((h1.mapped_bytes > h0.mapped_bytes ? h1.mapped_bytes - h0.mapped_bytes : 0) / 4096);
    int leak = nv ? ((perdidos > 2) || (h1.live_blocks > h0.live_blocks + 1)) : 0;
    (void)crec;
    int heap = kheap_check();
    int region = vmm_kernel_user_region_empty() ? 1 : 0;
    kprint("[USFUZZ] limpios: "); kprint_dec((uint32_t)clean); kprint(" | violaciones: "); kprint_dec((uint32_t)breach); kprint(" | excepciones: "); kprint_dec((uint32_t)faulted);
    kprint(" | plazo: "); kprint_dec((uint32_t)timeout); kprint(" | otros: "); kprint_dec((uint32_t)(other + norun)); kprint(" | "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t_start)); kprint(" ms\n");
    kprint("[USFUZZ] marcos libres antes/despues: "); kprint_dec((uint32_t)f0); kprint("/"); kprint_dec((uint32_t)f1); kprint(" (monton crecio "); kprint_dec((uint32_t)crec);
    kprint(" marcos, bloques vivos "); kprint_dec((uint32_t)h0.live_blocks); kprint("->"); kprint_dec((uint32_t)h1.live_blocks); kprint(")"); kprint(nv ? (leak ? "  FUGA\n" : "  sin fugas\n") : "  (informativo)\n");
    kprint("[USFUZZ] monton: "); kprint(heap == 0 ? "consistente" : "DANADO"); kprint(" | PML4 del kernel sin entradas de usuario: "); kprint(region ? "si" : "NO"); kprint("\n");
    int ok = clean == nproc && !leak && heap == 0 && region;
    if (!nv) { nivel = 1; int ok2 = usermode_fuzz(3, 300, seed ^ 0x5a5a5u); nivel = 0; ok = ok && ok2; }
    kprint(ok ? "[USFUZZ] TODO OK: el kernel resistio sin ninguna violacion\n" : "[USFUZZ] HAY FALLOS\n");
    return ok;
}

/* ================================================================== PARCHE 118: memoria agotada (OOM) en el cargador de procesos
 * Para n = 0, 1, 2... hace que la asignacion de marcos fisicos n+1 (y todas las siguientes) falle MIENTRAS se crea un proceso de usuario. Cada fallo cae en un punto
 * distinto de usermode_exec / vmm / heap. Obligaciones: nunca cuelgue ni panico; un fallo devuelve error (negativo) y deja el sistema EXACTAMENTE como estaba (mismos
 * marcos libres, mismos bloques vivos del monton, tabla de procesos sin ranuras ocupadas); en cuanto n basta, el proceso arranca y termina bien. */
int usermode_oomtest(void)
{
    uint32_t ilen = 0; char *img = vfs_read_alloc("/bin/hello.elf", &ilen);
    if (!img || ilen == 0) { kprint("[OOM] no encuentro /bin/hello.elf\n"); if (img) kfree(img); return -1; }
    size_t f0 = 0, u0 = 0, t0 = 0, f1 = 0, u1 = 0, t1 = 0;
    int fail_at_first_ok = -1, bad = 0, errs = 0, exits_ok = 0;
    long hits_total = 0;
    /* pasada de calentamiento sin inyeccion (cosas de un solo uso) */
    { int e = 0; int ix = usermode_exec("oom", img, ilen, 5000, "", &e); if (ix >= 0) (void)usermode_wait(ix); thread_sleep(20); }
    struct kheap_counters h0, h1;
    kheap_get_counters(&h0); pmm_get_stats(&f0, &u0, &t0);
    kprint("[OOM] inyectando memoria agotada en cada punto de la carga de un proceso...\n");
    for (int n = 0; n < 200; ++n) {
        pmm_inject_oom_after(n);
        int e = 0; int ix = usermode_exec("oom", img, ilen, 5000, "", &e);
        unsigned long hits = pmm_inject_oom_hits();
        pmm_inject_oom_after(-1);
        if (ix < 0) {
            errs++; hits_total += (long)hits;
            if (hits == 0) { bad++; kprint("[OOM] n="); kprint_dec((uint32_t)n); kprint(": fallo sin que se negara ningun marco\n"); }
        } else {
            int64_t code = usermode_wait(ix);
            if (code == 0) exits_ok++; else { bad++; kprint("[OOM] n="); kprint_dec((uint32_t)n); kprint(": el proceso arranco pero salio con codigo distinto de 0\n"); }
            if (fail_at_first_ok < 0) fail_at_first_ok = n;
            if (hits == 0) break;                         /* ya no se denego ningun marco: hemos barrido todos los puntos */
        }
        thread_sleep(5);
        pmm_get_stats(&f1, &u1, &t1); kheap_get_counters(&h1);
        if (f1 != f0 || h1.live_blocks != h0.live_blocks) {
            bad++; kprint("[OOM] n="); kprint_dec((uint32_t)n); kprint(": el sistema no quedo como estaba (marcos libres "); kprint_dec((uint32_t)f0); kprint("->"); kprint_dec((uint32_t)f1);
            kprint(", bloques "); kprint_dec((uint32_t)h0.live_blocks); kprint("->"); kprint_dec((uint32_t)h1.live_blocks); kprint(")\n");
            f0 = f1; h0 = h1;                              /* re-base para no repetir el mismo aviso en todas las vueltas */
        }
    }
    kfree(img);
    int heap = kheap_check();
    kprint("[OOM] cargas fallidas sin dano: "); kprint_dec((uint32_t)errs); kprint(" | denegaciones de marco: "); kprint_dec((uint32_t)hits_total);
    kprint(" | primera carga completa con n="); kprint_dec((uint32_t)(fail_at_first_ok < 0 ? 0 : fail_at_first_ok)); kprint(" | defectos: "); kprint_dec((uint32_t)bad);
    kprint(" | monton: "); kprint(heap == 0 ? "consistente\n" : "DANADO\n");
    int ok = bad == 0 && heap == 0 && errs >= 3 && exits_ok >= 1;
    kprint(ok ? "[OOM] TODO OK\n" : "[OOM] HAY FALLOS\n");
    return ok;
}

/* ================================================================== PARCHE 119: memoria agotada en el monton y en el sistema de archivos
 * Cada ronda pide MAS de lo que cabe en el mayor hueco libre (obliga al monton a crecer) y se repite con el PMM fallando en el marco 0, k, 2k... hasta cubrir toda la
 * peticion; la ultima repeticion va sin fallos.  Obligaciones:
 *  (1) kmalloc: devuelve NULL (nunca basura) y el monton queda consistente y SIN marcos perdidos tras cada fallo.
 *  (2) vfs_write sobre un archivo existente: si falla, el contenido ANTERIOR sigue intacto; si funciona, el archivo tiene el tamano pedido.
 *  (3) vfs_create: o existe entero o no existe.
 * El monton no se encoge, asi que cada ronda parte de un hueco mayor: pocas rondas y de tamano creciente. */
static int oom_same(const char *a, uint32_t al, const char *b, uint32_t bl) { if (al != bl) return 0; for (uint32_t i = 0; i < al; ++i) if (a[i] != b[i]) return 0; return 1; }
int usermode_oomtest_mem(void)
{
    int bad = 0, denied = 0, served = 0, fw_ok = 0, fw_fail = 0, fc_ok = 0, fc_fail = 0;
    long pt_lost = 0;       /* tablas de paginas que un crecimiento fallido deja creadas (se reutilizan despues; acotadas por el tamano del monton) */
    { void *w = kmalloc(4096); if (w) kfree(w); }
    for (int ronda = 0; ronda < 4; ++ronda) {
        size_t want = kheap_largest_free() + 64 * 1024;
        size_t frames = want / 4096 + 2;
        long step = (long)(frames / 24) + 1;
        for (long n = 0; n <= (long)frames + step; n += step) {
            int last = n > (long)frames;                          /* ultima vuelta: sin fallos */
            size_t f0 = 0, u0 = 0, t0 = 0, f1 = 0, u1 = 0, t1 = 0; struct kheap_counters h0, h1;
            pmm_get_stats(&f0, &u0, &t0); kheap_get_counters(&h0);
            pmm_inject_oom_after(last ? -1 : n);
            uint8_t *p = (uint8_t *)kmalloc(want);
            unsigned long hits = pmm_inject_oom_hits();
            pmm_inject_oom_after(-1);
            if (!p) {
                denied++;
                if (last) { bad++; kprint("[OOM] monton: la peticion sin fallos tampoco se sirvio\n"); }
                pmm_get_stats(&f1, &u1, &t1); kheap_get_counters(&h1);
                if (f0 >= f1 && f0 - f1 <= 2 && h1.mapped_bytes == h0.mapped_bytes && h1.live_blocks == h0.live_blocks) pt_lost += (long)(f0 - f1);
                else { bad++; kprint("[OOM] monton: un fallo dejo marcos/bloques perdidos (n="); kprint_dec((uint32_t)n); kprint(")\n"); }
            } else {
                served++; if (hits) bad++;                      /* servida pese a haber denegado marcos */
                p[0] = 1; p[want - 1] = 2; if (p[0] != 1 || p[want - 1] != 2) bad++;
                kfree(p);
            }
            if (kheap_check() != 0) { bad++; kprint("[OOM] monton danado, ronda "); kprint_dec((uint32_t)ronda); kprint("\n"); return 0; }
        }
        /* --- sistema de archivos: mismo barrido con un archivo cuyo tamano exige crecer el monton (ruta /tmp/: efimera, sin disco) --- */
        size_t fwant = kheap_largest_free() + 64 * 1024; if (fwant > 16u * 1024 * 1024) fwant = 16u * 1024 * 1024;
        size_t ffr = fwant / 4096 + 2; long fstep = (long)(ffr / 24) + 1;
        const char *orig = "CONTENIDO ORIGINAL"; uint32_t ol = 18;
        for (long n = 0; n <= (long)ffr + fstep; n += fstep) {
            int last = n > (long)ffr;
            vfs_delete("/tmp/oomA"); vfs_delete("/tmp/oomB");
            vfs_create("/tmp/oomA", orig);
            pmm_inject_oom_after(last ? -1 : n);
            int r = vfs_write("/tmp/oomA", 0, (uint32_t)fwant);
            int c = vfs_create("/tmp/oomB", "NUEVO");
            pmm_inject_oom_after(-1);
            uint32_t l = 0; char *back = vfs_read_alloc("/tmp/oomA", &l);
            if (!back) { bad++; kprint("[OOM] vfs: el archivo desaparecio\n"); }
            else {
                if (r >= 0) { fw_ok++; if (l != (uint32_t)fwant) { bad++; kprint("[OOM] vfs: escritura aceptada con tamano incorrecto\n"); } }
                else { fw_fail++; if (last) { bad++; kprint("[OOM] vfs: la escritura sin fallos fallo\n"); } if (!oom_same(back, l, orig, ol)) { bad++; kprint("[OOM] vfs: la escritura fallo y ESTROPEO el contenido anterior\n"); } }
                kfree(back);
            }
            uint32_t l2 = 0; char *b2 = vfs_read_alloc("/tmp/oomB", &l2);
            if (c >= 0) { fc_ok++; if (!b2 || !oom_same(b2, l2, "NUEVO", 5)) { bad++; kprint("[OOM] vfs: creacion aceptada pero archivo incompleto\n"); } }
            else { fc_fail++; if (b2) { bad++; kprint("[OOM] vfs: creacion fallida dejo un archivo\n"); } }
            if (b2) kfree(b2);
            if (kheap_check() != 0) { bad++; kprint("[OOM] monton danado tras vfs\n"); return 0; }
        }
        vfs_delete("/tmp/oomA"); vfs_delete("/tmp/oomB");
    }
    { struct kheap_counters hz; kheap_get_counters(&hz);
      long cota = (long)(hz.mapped_bytes / (2u * 1024 * 1024)) + 4;
      if (pt_lost > cota) { bad++; kprint("[OOM] monton: demasiadas tablas de paginas sin reutilizar: "); kprint_dec((uint32_t)pt_lost); kprint(" > "); kprint_dec((uint32_t)cota); kprint("\n"); } }
    kprint("[OOM] monton: "); kprint_dec((uint32_t)denied); kprint(" peticiones denegadas, "); kprint_dec((uint32_t)served); kprint(" servidas, tablas de paginas retenidas "); kprint_dec((uint32_t)pt_lost); kprint("\n");
    kprint("[OOM] vfs: escrituras ok/rechazadas "); kprint_dec((uint32_t)fw_ok); kprint("/"); kprint_dec((uint32_t)fw_fail);
    kprint(" | creaciones ok/rechazadas "); kprint_dec((uint32_t)fc_ok); kprint("/"); kprint_dec((uint32_t)fc_fail); kprint(" | defectos: "); kprint_dec((uint32_t)bad); kprint("\n");
    int ok = bad == 0 && denied > 20 && served >= 4 && fw_fail > 20 && fw_ok >= 4;
    kprint(ok ? "[OOM] MEM TODO OK\n" : "[OOM] MEM HAY FALLOS\n");
    return ok;
}
