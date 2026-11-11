/* Programa de usuario hostil: intenta escribir en memoria del kernel. Debe morir con #PF sin tumbar el SO. */
#include "ulib.h"

void _start(void)
{
    u_puts("[fault] voy a escribir en memoria del kernel...\n");
    *(volatile int *)0x100000 = 1;
    u_puts("[fault] ERROR: la escritura no fallo\n");
    u_exit(99);
}
