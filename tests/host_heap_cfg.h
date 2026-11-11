/* Configuracion de pruebas del heap (se inyecta con -include en test_heap).
 * El heap usa una arena estatica en vez de la direccion virtual fija del kernel. */
#ifndef HOST_HEAP_CFG_H
#define HOST_HEAP_CFG_H

#include <stdint.h>

#define HOST_ARENA_SIZE   (8ULL * 1024 * 1024)

extern uint8_t host_heap_arena[HOST_ARENA_SIZE];

#define SOMA_HOST_TEST    1            /* mem.c: no redefine memcpy/memset/... */
#define SOMA_KHEAP_POISON 1            /* rellena de 0xDD lo liberado */
#define KHEAP_START       ((uint64_t)(uintptr_t)host_heap_arena)
#define KHEAP_MAX_SIZE    HOST_ARENA_SIZE

#endif
