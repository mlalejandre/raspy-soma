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
uint64_t virtio_blk_capacity(void);   /* sectores de 512 B; 0 si no se conoce */

#endif
