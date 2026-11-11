/* Arena estatica del heap para las pruebas de mem.c en host. */
#include <stdint.h>
#include "host_heap_cfg.h"

uint8_t host_heap_arena[HOST_ARENA_SIZE] __attribute__((aligned(4096)));
