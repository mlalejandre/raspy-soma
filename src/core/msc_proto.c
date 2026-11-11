/* PARCHE 049: ver msc_proto.h */
#include "msc_proto.h"

static void put32le(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put32be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t get32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3]; }
static void clr(uint8_t *p, int n) { for (int i = 0; i < n; i++) p[i] = 0; }

void msc_cbw_build(uint8_t cbw[MSC_CBW_LEN], uint32_t tag, uint32_t xfer_len, int dir_in, uint8_t lun, const uint8_t *cdb, uint8_t cdb_len)
{
    clr(cbw, MSC_CBW_LEN);
    put32le(cbw, MSC_SIG_CBW); put32le(cbw + 4, tag); put32le(cbw + 8, xfer_len);
    cbw[12] = (uint8_t)(dir_in ? 0x80 : 0x00); cbw[13] = lun & 0x0F;
    if (cdb_len > 16) cdb_len = 16;
    cbw[14] = cdb_len;
    for (int i = 0; i < cdb_len; i++) cbw[15 + i] = cdb[i];
}

int msc_csw_parse(const uint8_t csw[MSC_CSW_LEN], uint32_t expect_tag, uint32_t *residue, uint8_t *status)
{
    if (get32le(csw) != MSC_SIG_CSW) return -1;
    if (get32le(csw + 4) != expect_tag) return -2;
    *residue = get32le(csw + 8); *status = csw[12];
    return 0;
}

int msc_cdb_inquiry(uint8_t *c, uint8_t alloc) { clr(c, 16); c[0] = 0x12; c[4] = alloc; return 6; }
int msc_cdb_tur(uint8_t *c) { clr(c, 16); return 6; }
int msc_cdb_request_sense(uint8_t *c, uint8_t alloc) { clr(c, 16); c[0] = 0x03; c[4] = alloc; return 6; }
int msc_cdb_read_capacity10(uint8_t *c) { clr(c, 16); c[0] = 0x25; return 10; }
int msc_cdb_rw10(uint8_t *c, int write, uint32_t lba, uint16_t n)
{
    clr(c, 16); c[0] = write ? 0x2A : 0x28; put32be(c + 2, lba); c[7] = (uint8_t)(n >> 8); c[8] = (uint8_t)n; return 10;
}
int msc_cdb_sync10(uint8_t *c) { clr(c, 16); c[0] = 0x35; return 10; }

int msc_parse_capacity10(const uint8_t d[8], uint64_t *sectors, uint32_t *block)
{
    uint32_t last = get32be(d); *block = get32be(d + 4);
    if (last == 0xFFFFFFFFu) return -1;
    *sectors = (uint64_t)last + 1;
    return 0;
}

static void trim(char *dst, const uint8_t *s, int n)
{
    int last = -1;
    for (int i = 0; i < n; i++) { uint8_t c = s[i]; dst[i] = (c >= 32 && c < 127) ? (char)c : ' '; if (dst[i] != ' ') last = i; }
    dst[last + 1] = 0;
}

void msc_parse_inquiry(const uint8_t d[36], char vendor[9], char product[17], uint8_t *devtype)
{
    *devtype = d[0] & 0x1F; trim(vendor, d + 8, 8); trim(product, d + 16, 16);
}
