#ifndef MYOS_BIGFS_GLUE_H
#define MYOS_BIGFS_GLUE_H

#include <stdint.h>
#include "bigfs.h"

/* SOMA_PARCHE_018_BIGFS: pegamento del BIGFS con el kernel (disco virtio, heap, reloj). */
void bigfs_sys_probe(void);                    /* arranque: informa si la region esta formateada (no monta) */
struct bigfs *bigfs_sys(int *err);             /* monta la primera vez (NUNCA formatea solo) */
int  bigfs_sys_format(void);                   /* formatea la region de BIGFS del disco y la monta */
/* SOMA_PARCHE_021_FPU: si el almacen esta formateado y existe /autoexec.cmd, ejecuta sus lineas con run() (una orden
 * por linea; se ignoran las vacias y las que empiezan por #). Devuelve cuantas ejecuto. */
int  bigfs_sys_autoexec(void (*run)(const char *line));
int  bigfs_sys_command(const char *args, char *out, uint32_t max);   /* comando de shell 'big ...' */

#endif
