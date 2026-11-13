/* PARCHE 102 (Fase 1, seguridad): validacion pura de rangos de usuario, sin dependencias del kernel para poder
 * probarla en host con fuzzing. La usa usermode.c: un rango solo es valido si esta entero dentro de la ventana
 * [base, base+window), no se desborda y cada pagina esta presente, con el bit U y (si se escribe) escribible. */
#ifndef SOMA_UACCESS_H
#define SOMA_UACCESS_H
#include <stdint.h>

#define UACC_PRESENT  (1ULL << 0)
#define UACC_WRITABLE (1ULL << 1)
#define UACC_USER     (1ULL << 2)

typedef uint64_t (*uaccess_pte_fn)(void *ctx, uint64_t va);

static inline int uaccess_range_ok(uint64_t p, uint64_t len, int need_write, uint64_t base, uint64_t window,
                                   uaccess_pte_fn pte_of, void *ctx)
{
    if (len == 0 || len > window || !pte_of) return 0;
    if (p < base || p - base > window - len) return 0;          /* sin sumas que puedan desbordarse */
    uint64_t end = p + len;                                      /* <= base+window: no desborda */
    for (uint64_t a = p & ~0xFFFULL; a < end; a += 4096) {
        uint64_t pte = pte_of(ctx, a);
        if (!(pte & UACC_PRESENT) || !(pte & UACC_USER)) return 0;
        if (need_write && !(pte & UACC_WRITABLE)) return 0;
    }
    return 1;
}
#endif
