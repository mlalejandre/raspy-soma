/* Pruebas en host de los espacios de direcciones de usuario (src/arch/x86_64/vmm.c, compilado con -DVMM_HOST_TEST):
 * el "PMM" entrega memoria real alineada, asi las tablas de paginas se recorren como en el kernel. Comprueba que no se fugan
 * marcos, que los fallos del PMM en cualquier punto no dejan basura ni corrompen, y que el espacio del kernel no se toca. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "vmm.h"
#include "pmm.h"

/* simbolos que vmm.c (con VMM_HOST_TEST) espera del enlazador; con nombre propio para valer en Linux y en macOS */
char __host_text_start[1], __host_text_end[1], __host_rodata_start[1], __host_rodata_end[1], __host_data_start[1], __host_data_end[1],
     __host_bss_start[1], __host_bss_end[1];

static uint64_t *g_kpml4;
static uint64_t g_cr3_actual;
void vmm_host_loaded(uint64_t as) { g_cr3_actual = as; }
uint64_t vmm_host_cr3(void) { return (uint64_t)(uintptr_t)g_kpml4; }

static int g_live, g_fail_after = -1;
#define MAXF 4096
static uintptr_t g_frames[MAXF];
uintptr_t pmm_alloc_frame(void)
{
    if (g_fail_after == 0) return 0;
    if (g_fail_after > 0) g_fail_after--;
    void *p = 0;
    if (posix_memalign(&p, 4096, 4096)) return 0;
    for (int i = 0; i < MAXF; ++i) if (!g_frames[i]) { g_frames[i] = (uintptr_t)p; g_live++; return (uintptr_t)p; }
    free(p); return 0;
}
void pmm_free_frame(uintptr_t f)
{
    for (int i = 0; i < MAXF; ++i) if (g_frames[i] == f) { g_frames[i] = 0; g_live--; free((void *)f); return; }
    printf("  FALLO: se libero un marco que no estaba vivo 0x%lx\n", (unsigned long)f); soma_t_fails++;
}

#define UB VMM_USER_REGION_BASE
#define RW (VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE)

static void test_ciclo_sin_fugas(void)
{
    int base = g_live;
    for (int r = 0; r < 50; ++r) {
        uint64_t as = vmm_as_create();
        CHECK(as != 0);
        for (int i = 0; i < 700; ++i)                          /* cruza varias PT (cada 512 paginas) y una PD */
            CHECK_EQ_INT(vmm_as_map(as, UB + (uint64_t)i * 4096, 0x5000 + (uint64_t)i * 4096, RW), 0);
        CHECK(vmm_as_map(as, UB + 0x40000000ULL, 0x9000, RW) == 0);   /* otra PDPT/PD */
        vmm_as_destroy(as);
    }
    CHECK_EQ_INT(g_live, base);
}
static void test_pte_y_rechazos(void)
{
    uint64_t as = vmm_as_create();
    CHECK_EQ_INT(vmm_as_map(as, UB + 0x3000, 0xABC000, RW), 0);
    uint64_t pte = vmm_as_get_pte(as, UB + 0x3000);
    CHECK((pte & VMM_FLAG_USER) && (pte & VMM_FLAG_PRESENT) && (pte & 0x000FFFFFFFFFF000ULL) == 0xABC000);
    CHECK_EQ_INT((int)vmm_as_get_pte(as, UB + 0x4000), 0);     /* sin mapear */
    CHECK(vmm_as_map(as, 0x1000, 0x1000, RW) == -1);           /* el usuario nunca mapea fuera de su PML4[1] */
    CHECK(vmm_as_map(as, 0xFFFFFFFF80000000ULL, 0x1000, RW) == -1);
    CHECK(vmm_as_map(0, UB, 0x1000, RW) == -1);
    vmm_as_destroy(as);
    vmm_as_destroy(0);                                         /* destruir 0 no hace nada */
}
static void test_kernel_intacto(void)
{
    uint64_t copia[512]; memcpy(copia, g_kpml4, sizeof copia);
    uint64_t as = vmm_as_create();
    uint64_t *p = (uint64_t *)(uintptr_t)as;
    for (int i = 0; i < 512; ++i)
        if (i != VMM_USER_PML4_INDEX) CHECK(p[i] == copia[i]);   /* comparte las tablas del kernel */
    for (int i = 0; i < 64; ++i) vmm_as_map(as, UB + (uint64_t)i * 4096, 0x7000, RW);
    CHECK(memcmp(copia, g_kpml4, sizeof copia) == 0);           /* mapear en un proceso no toca el PML4 del kernel */
    CHECK_EQ_INT(vmm_kernel_user_region_empty(), 1);
    vmm_as_destroy(as);
    CHECK(memcmp(copia, g_kpml4, sizeof copia) == 0);
}
static void test_fallos_inyectados(void)
{
    /* el PMM falla en el n-esimo marco: sin fugas, sin marcos dobles, y el espacio sigue siendo destruible */
    for (int n = 0; n < 12; ++n) {
        int base = g_live;
        g_fail_after = n;
        uint64_t as = vmm_as_create();
        int mapeadas = 0;
        if (as) for (int i = 0; i < 600; ++i)
            if (vmm_as_map(as, UB + (uint64_t)i * 4096 + (i % 2 ? 0x40000000ULL : 0), 0x6000, RW) == 0) mapeadas++;
        g_fail_after = -1;
        if (as) vmm_as_destroy(as);
        CHECK_EQ_INT(g_live, base);
        (void)mapeadas;
    }
}
static void test_destruir_el_activo(void)
{
    uint64_t as = vmm_as_create();
    vmm_as_map(as, UB, 0x5000, RW);
    vmm_load_as(as);
    CHECK(g_cr3_actual == as);
    vmm_as_destroy(as);                                         /* PARCHE 104: debe volver antes al espacio del kernel */
    CHECK(g_cr3_actual == vmm_kernel_as());                     /* CR3 ya no apunta a tablas liberadas */
    CHECK_EQ_INT(g_live, 0);
}
int main(void)
{
    void *k = 0; posix_memalign(&k, 4096, 4096); memset(k, 0, 4096); g_kpml4 = (uint64_t *)k;
    for (int i = 0; i < 512; ++i) if (i != VMM_USER_PML4_INDEX) g_kpml4[i] = 0x1000ULL * (uint64_t)(i + 1) | RW;   /* entradas de kernel simuladas */
    test_ciclo_sin_fugas(); test_pte_y_rechazos(); test_kernel_intacto(); test_fallos_inyectados(); test_destruir_el_activo();
    CHECK_EQ_INT(g_live, 0);
    TEST_MAIN_END("vmm");
}
