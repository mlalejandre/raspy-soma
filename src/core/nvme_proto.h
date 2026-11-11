#ifndef SOMA_NVME_PROTO_H
#define SOMA_NVME_PROTO_H
/* PARCHE 043: logica pura del protocolo NVMe (colas, comandos, PRP, fases de finalizacion). No toca hardware: los
 * punteros de timbre (doorbell) y las colas son memoria normal, asi que se prueba en el host contra un modelo
 * simulado del controlador (tests/test_nvme.c). */
#include <stdint.h>
#include <stddef.h>

struct nvme_sqe { uint32_t cdw0, nsid; uint64_t rsvd, mptr, prp1, prp2; uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15; };   /* 64 bytes */
struct nvme_cqe { uint32_t dw0, dw1; uint16_t sqhd, sqid; uint16_t cid, status; };                                              /* 16 bytes */

struct nvme_q {
    struct nvme_sqe *sq; struct nvme_cqe *cq;
    volatile uint32_t *sq_db, *cq_db;
    uint16_t depth, sq_tail, cq_head, phase, next_cid;
};

#define NVME_ADMIN_DELETE_SQ 0x00
#define NVME_ADMIN_CREATE_SQ 0x01
#define NVME_ADMIN_CREATE_CQ 0x05
#define NVME_ADMIN_IDENTIFY  0x06
#define NVME_IO_FLUSH 0x00
#define NVME_IO_WRITE 0x01
#define NVME_IO_READ  0x02

void     nvme_q_init(struct nvme_q *q, struct nvme_sqe *sq, struct nvme_cqe *cq, volatile uint32_t *sq_db, volatile uint32_t *cq_db, uint16_t depth);
/* Encola un comando (le asigna cid) y avisa por el timbre. Devuelve el cid, o -1 si la cola esta llena. */
int      nvme_submit(struct nvme_q *q, struct nvme_sqe *e, uint16_t sq_head_seen);
/* Si hay una finalizacion nueva la consume (avanza cq_head, cambia la fase al dar la vuelta, toca el timbre) y devuelve 1. */
int      nvme_poll(struct nvme_q *q, struct nvme_cqe *out);
/* PRP para `bytes` (1..) a partir de `data` (alineada a pagina de 4 KiB); `list` = pagina para la lista cuando hacen falta 3 o mas
 * paginas. Devuelve 0 o -1 si no cabe (maximo 512 paginas) o la alineacion es incorrecta. */
int      nvme_prp(uint64_t data, uint64_t list, uint32_t bytes, uint64_t *prp1, uint64_t *prp2);
void     nvme_rw_sqe(struct nvme_sqe *e, uint8_t op, uint32_t nsid, uint64_t lba, uint32_t count, uint64_t prp1, uint64_t prp2);
int      nvme_status_ok(const struct nvme_cqe *c);                                  /* codigo de estado 0 y tipo generico */
#endif
