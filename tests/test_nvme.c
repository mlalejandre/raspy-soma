/* PARCHE 043: pruebas del protocolo NVMe (colas, fases, PRP, lectura/escritura) contra un controlador simulado. ASAN+UBSAN. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/nvme_proto.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define DEPTH 16
#define DISK_SECTORS 4096
static uint8_t *disk;
static volatile uint32_t sq_db_reg, cq_db_reg;
static uint16_t m_sq_head, m_cq_tail, m_phase = 1;

/* modelo del controlador: procesa lo que el host dejo en la SQ */
static void ctrl_run(struct nvme_q *q)
{
    while (m_sq_head != (uint16_t)sq_db_reg) {
        struct nvme_sqe *e = &q->sq[m_sq_head];
        uint16_t status = 0;
        uint8_t op = e->cdw0 & 0xFF; uint64_t lba = ((uint64_t)e->cdw11 << 32) | e->cdw10; uint32_t cnt = (e->cdw12 & 0xFFFF) + 1;
        if (op == NVME_IO_READ || op == NVME_IO_WRITE) {
            if (lba + cnt > DISK_SECTORS) status = 0x80 << 1;                                   /* LBA fuera de rango */
            else {
                uint32_t bytes = cnt * 512, pages = (bytes + 4095) / 4096;
                for (uint32_t pg = 0; pg < pages; pg++) {
                    uint64_t pa = pg == 0 ? e->prp1 : (pages == 2 ? e->prp2 : ((uint64_t *)(uintptr_t)e->prp2)[pg - 1]);
                    uint32_t n = bytes - pg * 4096; if (n > 4096) n = 4096;
                    if (op == NVME_IO_READ) memcpy((void *)(uintptr_t)pa, disk + lba * 512 + pg * 4096, n);
                    else memcpy(disk + lba * 512 + pg * 4096, (void *)(uintptr_t)pa, n);
                }
            }
        }
        struct nvme_cqe *c = &q->cq[m_cq_tail];
        c->dw0 = 0; c->sqhd = (uint16_t)((m_sq_head + 1) % DEPTH); c->sqid = 1; c->cid = (uint16_t)(e->cdw0 >> 16);
        c->status = (uint16_t)(status | m_phase);
        if (++m_cq_tail == DEPTH) { m_cq_tail = 0; m_phase ^= 1; }
        m_sq_head = (uint16_t)((m_sq_head + 1) % DEPTH);
    }
}

static struct nvme_q Q;
static uint16_t host_sq_head;

static int do_io(uint8_t op, uint64_t lba, uint32_t count, void *buf, uint8_t *dmabuf, uint64_t *list, int *status_ok)
{
    struct nvme_sqe e; uint64_t p1, p2;
    if (op != NVME_IO_FLUSH) {
        if (op == NVME_IO_WRITE) memcpy(dmabuf, buf, count * 512);
        if (nvme_prp((uint64_t)(uintptr_t)dmabuf, (uint64_t)(uintptr_t)list, count * 512, &p1, &p2) != 0) return -2;
    } else p1 = p2 = 0;
    nvme_rw_sqe(&e, op, 1, lba, count, p1, p2);
    int cid = nvme_submit(&Q, &e, host_sq_head);
    if (cid < 0) return -1;
    ctrl_run(&Q);
    struct nvme_cqe c; int got = nvme_poll(&Q, &c);
    CHECK(got == 1); if (!got) return -3;
    CHECK(c.cid == cid);
    host_sq_head = c.sqhd;
    *status_ok = nvme_status_ok(&c);
    if (op == NVME_IO_READ && *status_ok) memcpy(buf, dmabuf, count * 512);
    return 0;
}

int main(void)
{
    disk = calloc(DISK_SECTORS, 512);
    struct nvme_sqe *sq = aligned_alloc(4096, 4096); struct nvme_cqe *cq = aligned_alloc(4096, 4096);
    uint8_t *dma = aligned_alloc(4096, 128 * 512); uint64_t *list = aligned_alloc(4096, 4096);
    nvme_q_init(&Q, sq, cq, &sq_db_reg, &cq_db_reg, DEPTH);
    CHECK(sizeof(struct nvme_sqe) == 64 && sizeof(struct nvme_cqe) == 16);

    uint8_t *ref = calloc(DISK_SECTORS, 512), *buf = malloc(128 * 512);
    unsigned seed = 7; int st;
    /* mas de 3 vueltas completas de la cola: prueba la fase y el ajuste del indice */
    for (int i = 0; i < 200; i++) {
        seed = seed * 1103515245u + 12345u;
        uint32_t cnt = 1 + (seed >> 8) % 128; uint64_t lba = (seed >> 3) % (DISK_SECTORS - cnt);
        if (i & 1) {
            for (uint32_t k = 0; k < cnt * 512; k++) buf[k] = (uint8_t)(seed + k * 13 + i);
            CHECK(do_io(NVME_IO_WRITE, lba, cnt, buf, dma, list, &st) == 0 && st);
            memcpy(ref + lba * 512, buf, cnt * 512);
        } else {
            memset(buf, 0xEE, cnt * 512);
            CHECK(do_io(NVME_IO_READ, lba, cnt, buf, dma, list, &st) == 0 && st);
            CHECK(memcmp(buf, ref + lba * 512, cnt * 512) == 0);
        }
    }
    CHECK(Q.phase == m_phase);
    CHECK(do_io(NVME_IO_FLUSH, 0, 0, 0, dma, list, &st) == 0 && st);
    /* error de dispositivo: LBA fuera del disco */
    CHECK(do_io(NVME_IO_READ, DISK_SECTORS - 1, 8, buf, dma, list, &st) == 0 && !st);
    /* PRP: casos limite */
    uint64_t a, b;
    CHECK(nvme_prp(0x1000, 0, 4096, &a, &b) == 0 && a == 0x1000 && b == 0);
    CHECK(nvme_prp(0x1000, 0, 4097, &a, &b) == 0 && b == 0x2000);
    CHECK(nvme_prp(0x1001, 0, 512, &a, &b) != 0);                      /* sin alinear */
    CHECK(nvme_prp(0x1000, 0, 12288, &a, &b) != 0);                    /* hace falta lista */
    CHECK(nvme_prp(0x1000, 0x2000, 0, &a, &b) != 0);                   /* tamano 0 */
    CHECK(nvme_prp(0x1000, 0x2001, 12288, &a, &b) != 0);               /* lista sin alinear */
    /* cola llena: sin consumir, tras DEPTH-1 envios el siguiente se rechaza */
    struct nvme_q Q2; nvme_q_init(&Q2, sq, cq, &sq_db_reg, &cq_db_reg, DEPTH);
    struct nvme_sqe e; nvme_rw_sqe(&e, NVME_IO_FLUSH, 1, 0, 0, 0, 0);
    int ok = 0; for (int i = 0; i < DEPTH; i++) if (nvme_submit(&Q2, &e, 0) >= 0) ok++;
    CHECK(ok == DEPTH - 1);
    /* cid nunca 0 y distinto en envios consecutivos */
    struct nvme_q Q3; nvme_q_init(&Q3, sq, cq, &sq_db_reg, &cq_db_reg, DEPTH); Q3.next_cid = 0xFFFF;
    int c1 = nvme_submit(&Q3, &e, 5), c2 = nvme_submit(&Q3, &e, 5);
    CHECK(c1 == 0xFFFF && c2 == 1);
    printf(fails ? "nvme_proto: %d FALLOS\n" : "nvme_proto: todo correcto\n", fails);
    return fails != 0;
}
