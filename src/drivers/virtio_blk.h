#ifndef MYOS_VIRTIO_BLK_H
#define MYOS_VIRTIO_BLK_H

#include <stdint.h>

/* Inicializa el controlador de disco duro VirtIO. Devuelve 1 si OK. */
int virtio_blk_init(void);

/* Lee un sector de 512 bytes del disco duro (LBA). 0 = OK, -1 = Error. */
int virtio_blk_read(uint64_t sector, void *buf);

/* Escribe un sector de 512 bytes en el disco duro (LBA). 0 = OK, -1 = Error. */
int virtio_blk_write(uint64_t sector, const void *buf);

/* SOMA_PARCHE_018_BIGFS: E/S de varios sectores en UNA peticion (hasta 128; trocea si son mas) y capacidad */
int virtio_blk_read_n(uint64_t sector, void *buf, uint32_t nsec);
int virtio_blk_write_n(uint64_t sector, const void *buf, uint32_t nsec);
/* PARCHE 115: barrera de escritura. Vuelve cuando todo lo escrito antes es durable. Este controlador NO negocia VIRTIO_BLK_F_FLUSH, asi que por la
 * especificacion de virtio el dispositivo se comporta como write-through y cada escritura completada ya es durable y ordenada: no hay nada que vaciar.
 * Es el punto donde un controlador con cache volatil (AHCI, NVMe, SD) debera emitir su FLUSH CACHE. 0 = OK. */
int virtio_blk_flush(void);
uint64_t virtio_blk_capacity(void);   /* sectores de 512 B; 0 si no se conoce */

#endif
