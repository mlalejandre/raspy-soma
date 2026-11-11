/* Programa de usuario de larga duracion: se turna con otros procesos (duerme entre mensajes). */
#include "ulib.h"

void _start(void)
{
    for (int i = 1; i <= 5; ++i) {
        u_puts("[ticker] pid="); u_putdec((u64)u_getpid()); u_puts(" tick "); u_putdec((u64)i); u_puts("\n");
        u_sleep(100);
    }
    u_exit(5);
}
