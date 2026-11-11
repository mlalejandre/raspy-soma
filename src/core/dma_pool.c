#include "dma_pool.h"
/* PARCHE 071 */
#define FRAME 4096u
void dma_pool_init(struct dma_pool *p, uintptr_t base, uintptr_t size) { p->base = base; p->size = size; p->cur = base; }
uintptr_t dma_pool_alloc(struct dma_pool *p, size_t nframes)
{
    if (!p->size || !nframes || nframes > p->size / FRAME) return 0;
    uintptr_t cur = (p->cur + FRAME - 1) & ~(uintptr_t)(FRAME - 1);
    uintptr_t need = (uintptr_t)nframes * FRAME;
    if (cur < p->base || cur - p->base > p->size || p->size - (cur - p->base) < need) return 0;
    p->cur = cur + need;
    return cur;
}
uintptr_t dma_pool_used(const struct dma_pool *p) { return p->cur - p->base; }
int dma_pool_contains(const struct dma_pool *p, uintptr_t phys, size_t len)
{
    return p->size && phys >= p->base && len <= p->size && phys - p->base <= p->size - len;
}
