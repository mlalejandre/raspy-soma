#ifndef SOMA_DMA_POOL_H
#define SOMA_DMA_POOL_H
#include <stdint.h>
#include <stddef.h>
/* PARCHE 071: reserva lineal de una zona fisica para DMA (en la Pi 5 se mapea como memoria normal NO cacheable, asi el hardware sin
 * coherencia de cache ve lo mismo que la CPU). Capa pura, probada en el host. */
struct dma_pool { uintptr_t base, size, cur; };
void dma_pool_init(struct dma_pool *p, uintptr_t base, uintptr_t size);
uintptr_t dma_pool_alloc(struct dma_pool *p, size_t nframes);       /* fisica, alineada a 4 KiB, a cero no garantizado; 0 si no cabe */
uintptr_t dma_pool_used(const struct dma_pool *p);
int dma_pool_contains(const struct dma_pool *p, uintptr_t phys, size_t len);
#endif
