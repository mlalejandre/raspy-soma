/* PARCHE 043: ver nvme_proto.h */
#include "nvme_proto.h"

static void zero(void *p, size_t n) { uint8_t *b = p; for (size_t i = 0; i < n; i++) b[i] = 0; }

void nvme_q_init(struct nvme_q *q, struct nvme_sqe *sq, struct nvme_cqe *cq, volatile uint32_t *sq_db, volatile uint32_t *cq_db, uint16_t depth)
{
    q->sq = sq; q->cq = cq; q->sq_db = sq_db; q->cq_db = cq_db; q->depth = depth;
    q->sq_tail = 0; q->cq_head = 0; q->phase = 1; q->next_cid = 1;
    zero(sq, (size_t)depth * sizeof *sq); zero(cq, (size_t)depth * sizeof *cq);
}

int nvme_submit(struct nvme_q *q, struct nvme_sqe *e, uint16_t sq_head_seen)
{
    uint16_t next = (uint16_t)((q->sq_tail + 1) % q->depth);
    if (next == sq_head_seen) return -1;                                       /* llena: la cola reserva una posicion */
    uint16_t cid = q->next_cid++; if (q->next_cid == 0) q->next_cid = 1;
    e->cdw0 = (e->cdw0 & 0xFFFFu) | ((uint32_t)cid << 16);
    q->sq[q->sq_tail] = *e;
    q->sq_tail = next;
    __sync_synchronize();                                   /* la orden debe ser visible antes del timbre */
    *q->sq_db = q->sq_tail;
    return cid;
}

int nvme_poll(struct nvme_q *q, struct nvme_cqe *out)
{
    volatile struct nvme_cqe *c = &q->cq[q->cq_head];
    if ((c->status & 1u) != q->phase) return 0;
    __sync_synchronize();                                   /* leer el resto de la entrada despues de ver la fase */
    *out = *(const struct nvme_cqe *)c;
    if (++q->cq_head == q->depth) { q->cq_head = 0; q->phase ^= 1u; }
    *q->cq_db = q->cq_head;
    return 1;
}

int nvme_prp(uint64_t data, uint64_t list, uint32_t bytes, uint64_t *prp1, uint64_t *prp2)
{
    if ((data & 0xFFF) || bytes == 0) return -1;
    uint32_t pages = (bytes + 4095u) / 4096u;
    if (pages > 512) return -1;
    *prp1 = data; *prp2 = 0;
    if (pages == 2) *prp2 = data + 4096;
    else if (pages > 2) {
        if (!list || (list & 0xFFF)) return -1;
        uint64_t *l = (uint64_t *)(uintptr_t)list;
        for (uint32_t i = 1; i < pages; i++) l[i - 1] = data + (uint64_t)i * 4096u;
        *prp2 = list;
    }
    return 0;
}

void nvme_rw_sqe(struct nvme_sqe *e, uint8_t op, uint32_t nsid, uint64_t lba, uint32_t count, uint64_t prp1, uint64_t prp2)
{
    zero(e, sizeof *e);
    e->cdw0 = op; e->nsid = nsid; e->prp1 = prp1; e->prp2 = prp2;
    e->cdw10 = (uint32_t)lba; e->cdw11 = (uint32_t)(lba >> 32); e->cdw12 = count ? count - 1u : 0;
}

int nvme_status_ok(const struct nvme_cqe *c) { return ((c->status >> 1) & 0x7FFF) == 0; }
