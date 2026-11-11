/* Programa de usuario de prueba: comprueba .rodata, .data (valor inicial) y .bss (a cero). */
#include "ulib.h"

static int contador = 41;          /* .data */
static volatile char vacio[256];   /* .bss: debe llegar a cero (volatile: que el compilador no lo elimine) */

void _start(void)
{
    u_puts("[hello] hola desde un ELF cargado del sistema de archivos, pid=");
    u_putdec((u64)u_getpid());
    u_puts("\n");
    contador++;
    int bss_ok = 1;
    for (int i = 0; i < 256; ++i) if (vacio[i] != 0) bss_ok = 0;
    u_puts(bss_ok && contador == 42 ? "[hello] .data y .bss correctos\n" : "[hello] ERROR en .data/.bss\n");
    u_exit(bss_ok && contador == 42 ? 0 : 1);
}
