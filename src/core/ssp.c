/* PARCHE 030: soporte del protector de pila del compilador (make SSP=1).
 * El canario es una constante de compilacion con un byte 0 y un salto de linea al principio (frena desbordamientos de
 * cadenas); detecta errores de programacion que pisan la direccion de retorno, no ataques dirigidos. Se mantiene fijo
 * durante toda la ejecucion a proposito: cambiarlo a mitad de arranque haria fallar a las funciones ya en curso. */
#include <stdint.h>
#include "console.h"
#include "arch.h"

uintptr_t __stack_chk_guard = (uintptr_t)0x0A0D5AC3A55A5A00ULL;

__attribute__((noreturn, noinline)) void __stack_chk_fail(void)
{
    kprint("\n*** SOMA: DESBORDAMIENTO DE PILA DETECTADO (protector de pila) ***\n");
    kprint("Retorno de funcion con el canario destruido: sistema detenido.\n");
    for (;;) arch_halt();
}
