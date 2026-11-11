/* PARCHE 049: pruebas de msc_proto (CBW/CSW, CDB SCSI, capacidad e INQUIRY). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/core/msc_proto.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void)
{
    uint8_t cdb[16], cbw[MSC_CBW_LEN];
    int l = msc_cdb_rw10(cdb, 0, 0x01020304, 0x0080);
    CHECK(l == 10 && cdb[0] == 0x28 && cdb[2] == 1 && cdb[3] == 2 && cdb[4] == 3 && cdb[5] == 4 && cdb[7] == 0 && cdb[8] == 0x80 && cdb[9] == 0 && cdb[10] == 0);
    l = msc_cdb_rw10(cdb, 1, 0xFFFFFFFFu, 1); CHECK(cdb[0] == 0x2A && cdb[2] == 0xFF && cdb[5] == 0xFF && cdb[8] == 1);
    CHECK(msc_cdb_inquiry(cdb, 36) == 6 && cdb[0] == 0x12 && cdb[4] == 36 && cdb[1] == 0);
    CHECK(msc_cdb_tur(cdb) == 6 && cdb[0] == 0);
    CHECK(msc_cdb_request_sense(cdb, 18) == 6 && cdb[0] == 3 && cdb[4] == 18);
    CHECK(msc_cdb_read_capacity10(cdb) == 10 && cdb[0] == 0x25);
    CHECK(msc_cdb_sync10(cdb) == 10 && cdb[0] == 0x35);
    msc_cdb_rw10(cdb, 0, 0x11223344, 0x0080);
    msc_cbw_build(cbw, 0xAABBCCDD, 65536, 1, 0, cdb, 10);
    CHECK(cbw[0] == 'U' && cbw[1] == 'S' && cbw[2] == 'B' && cbw[3] == 'C' && cbw[4] == 0xDD && cbw[7] == 0xAA);
    CHECK(cbw[8] == 0 && cbw[9] == 0 && cbw[10] == 1 && cbw[11] == 0 && cbw[12] == 0x80 && cbw[13] == 0 && cbw[14] == 10 && cbw[15] == 0x28 && cbw[17] == 0x11);
    msc_cbw_build(cbw, 1, 0, 0, 0x1F, cdb, 40);                          /* LUN de 4 bits, CDB recortado a 16 */
    CHECK(cbw[12] == 0 && cbw[13] == 0x0F && cbw[14] == 16);
    uint8_t csw[MSC_CSW_LEN] = { 'U', 'S', 'B', 'S', 7, 0, 0, 0, 0x10, 0, 0, 0, 1 }; uint32_t res = 0; uint8_t st = 9;
    CHECK(msc_csw_parse(csw, 7, &res, &st) == 0 && res == 16 && st == 1);
    CHECK(msc_csw_parse(csw, 8, &res, &st) == -2);
    csw[0] = 'X'; CHECK(msc_csw_parse(csw, 7, &res, &st) == -1);
    uint8_t cap[8] = { 0, 0, 0x3F, 0xFF, 0, 0, 2, 0 }; uint64_t sec = 0; uint32_t blk = 0;
    CHECK(msc_parse_capacity10(cap, &sec, &blk) == 0 && sec == 0x4000 && blk == 512);
    uint8_t big[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 2, 0 }; CHECK(msc_parse_capacity10(big, &sec, &blk) == -1);
    uint8_t inq[36]; memset(inq, ' ', sizeof inq); inq[0] = 0; memcpy(inq + 8, "QEMU    ", 8); memcpy(inq + 16, "QEMU HARDDISK   ", 16);
    char v[9], p[17]; uint8_t dt = 9; msc_parse_inquiry(inq, v, p, &dt);
    CHECK(dt == 0 && strcmp(v, "QEMU") == 0 && strcmp(p, "QEMU HARDDISK") == 0);
    memset(inq, 0, sizeof inq); inq[0] = 0x25; msc_parse_inquiry(inq, v, p, &dt); CHECK(dt == 5 && v[0] == 0 && p[0] == 0);
    printf(fails ? "msc_proto: %d FALLOS\n" : "msc_proto: todo correcto\n", fails);
    return fails != 0;
}
