/* Programa grande (~260 KiB de ELF): solo cabe en el almacen BIGFS (el VFS pequeno admite 16 KiB).
 * Comprueba que .data (256 KiB) llega entero y que .bss (1 MiB) llega a cero y es escribible. */
#include "ulib.h"

static volatile unsigned char tabla[262144] = { [0] = 7, [100000] = 9, [262143] = 5 };   /* .data: ocupa archivo */
static volatile unsigned char vacio[1048576];                                              /* .bss: 1 MiB */

void _start(void)
{
    int ok = tabla[0] == 7 && tabla[100000] == 9 && tabla[262143] == 5 && tabla[1] == 0 && tabla[262142] == 0;
    for (unsigned i = 0; i < sizeof vacio; ++i) if (vacio[i] != 0) { ok = 0; break; }
    for (unsigned i = 0; i < sizeof vacio; ++i) vacio[i] = (unsigned char)(i * 31u + 7u);
    for (unsigned i = 0; i < sizeof vacio; ++i) if (vacio[i] != (unsigned char)(i * 31u + 7u)) { ok = 0; break; }
    char a[64]; i64 n = u_getargs(a, sizeof a);
    u_puts(ok ? "[bigprog] OK: .data de 256 KiB y .bss de 1 MiB correctos" : "[bigprog] ERROR");
    if (n > 0) { u_puts(", args='"); u_puts(a); u_puts("'"); }
    u_puts("\n");
    u_exit(ok ? 0 : 1);
}
