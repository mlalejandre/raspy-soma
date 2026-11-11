#ifndef MYOS_PMM_H
#define MYOS_PMM_H

#include <stdint.h>
#include <stddef.h>

#define PMM_FRAME_SIZE 4096

/* Inicializa el PMM parseando el mapa E820 de Multiboot */
void pmm_init(uint32_t mb_magic, uint32_t mb_info_addr);

/* Asigna un marco fisico de 4 KiB (devuelve direccion fisica o 0 si OOM) */
uintptr_t pmm_alloc_frame(void);

/* Libera un marco fisico de 4 KiB */
void pmm_free_frame(uintptr_t phys_addr);

/* SOMA_PARCHE_020_AUDIO: n marcos CONTIGUOS (buffers DMA). Devuelve la direccion fisica o 0 si no hay un hueco. */
uintptr_t pmm_alloc_contig(size_t nframes);
void pmm_free_contig(uintptr_t phys_addr, size_t nframes);

/* Telemetria de memoria fisica */
void pmm_get_stats(size_t *free_frames, size_t *used_frames, size_t *total_frames);
void pmm_dump_stats(void);

/* Auto-test de asignacion, alineacion y liberacion */
int pmm_test_self(void);

#endif
