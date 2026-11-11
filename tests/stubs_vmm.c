/* Sustitutos de VMM/PMM para pruebas en host: contabilidad de marcos y fallos inyectables. */
#include <stdint.h>
#include <stddef.h>
#include "host_stubs.h"
#include "vmm.h"
#include "pmm.h"

static int g_fail_after = -1;     /* -1 = el PMM nunca falla */
static int g_frames_live;

void host_pmm_fail_after(int n) { g_fail_after = n; }
int  host_pmm_frames_live(void) { return g_frames_live; }

uintptr_t pmm_alloc_frame(void)
{
    if (g_fail_after == 0) return 0;
    if (g_fail_after > 0) g_fail_after--;
    g_frames_live++;
    return 0x100000u + (uintptr_t)g_frames_live * 4096u;
}
void pmm_free_frame(uintptr_t pa) { (void)pa; g_frames_live--; }

int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags)
{
    (void)va; (void)pa; (void)flags;
    return 0;                      /* las arenas ya estan mapeadas en el host */
}
int vmm_unmap_page(uint64_t va) { (void)va; return 0; }
uint64_t vmm_virt_to_phys(uint64_t va) { (void)va; return 0x200000u; }
