#include <assert.h>
#include <stdio.h>
#include "../src/core/dma_pool.h"
int main(void)
{
    struct dma_pool p;
    dma_pool_init(&p, 0x1E000000u, 0x400000u);                       /* 4 MiB */
    uintptr_t a = dma_pool_alloc(&p, 14), b = dma_pool_alloc(&p, 1);
    assert(a == 0x1E000000u && b == 0x1E000000u + 14 * 4096u && dma_pool_used(&p) == 15 * 4096u);
    assert(dma_pool_alloc(&p, 0) == 0 && dma_pool_alloc(&p, 1024) == 0);          /* 1024 marcos = 4 MiB: ya no caben */
    uintptr_t c = dma_pool_alloc(&p, 1024 - 15);
    assert(c == b + 4096u && dma_pool_used(&p) == 0x400000u);
    assert(dma_pool_alloc(&p, 1) == 0);                                            /* agotado */
    assert(dma_pool_contains(&p, a, 4096) && dma_pool_contains(&p, 0x1E3FF000u, 4096) && !dma_pool_contains(&p, 0x1E400000u, 1));
    assert(!dma_pool_contains(&p, 0x1DFFF000u, 4096) && !dma_pool_contains(&p, a, 0x400001u) && !dma_pool_contains(&p, a, (size_t)-1));
    struct dma_pool z; dma_pool_init(&z, 0, 0);
    assert(dma_pool_alloc(&z, 1) == 0 && !dma_pool_contains(&z, 0, 0));           /* sin pool (virt): siempre 0 */
    struct dma_pool w; dma_pool_init(&w, 0x1000, 0x2000);
    assert(dma_pool_alloc(&w, 3) == 0 && dma_pool_alloc(&w, 2) == 0x1000);
    puts("dma_pool: todo correcto");
    return 0;
}
