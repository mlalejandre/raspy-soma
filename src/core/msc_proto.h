#ifndef SOMA_MSC_PROTO_H
#define SOMA_MSC_PROTO_H
/* PARCHE 049: logica pura de USB Mass Storage "Bulk-Only Transport" (CBW/CSW) y de las ordenes SCSI minimas
 * (INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY(10), READ/WRITE(10), SYNCHRONIZE CACHE). Probada en el host. */
#include <stdint.h>
#define MSC_CBW_LEN 31
#define MSC_CSW_LEN 13
#define MSC_SIG_CBW 0x43425355u                       /* "USBC" */
#define MSC_SIG_CSW 0x53425355u                       /* "USBS" */
void msc_cbw_build(uint8_t cbw[MSC_CBW_LEN], uint32_t tag, uint32_t xfer_len, int dir_in, uint8_t lun, const uint8_t *cdb, uint8_t cdb_len);
/* 0 = CSW valido (residue y status rellenados); -1 firma incorrecta; -2 etiqueta distinta. status: 0 bien, 1 fallo, 2 error de fase. */
int  msc_csw_parse(const uint8_t csw[MSC_CSW_LEN], uint32_t expect_tag, uint32_t *residue, uint8_t *status);
/* Cada funcion rellena cdb (16 bytes como maximo, a ceros el resto) y devuelve la longitud de la orden. */
int  msc_cdb_inquiry(uint8_t *cdb, uint8_t alloc);
int  msc_cdb_tur(uint8_t *cdb);
int  msc_cdb_request_sense(uint8_t *cdb, uint8_t alloc);
int  msc_cdb_read_capacity10(uint8_t *cdb);
int  msc_cdb_rw10(uint8_t *cdb, int write, uint32_t lba, uint16_t n);
int  msc_cdb_sync10(uint8_t *cdb);
/* READ CAPACITY(10): 8 bytes. 0 = bien; -1 = capacidad >= 2 TiB (0xFFFFFFFF, haria falta READ CAPACITY(16)). */
int  msc_parse_capacity10(const uint8_t d[8], uint64_t *sectors, uint32_t *block);
/* INQUIRY (36 bytes): cadenas recortadas (ASCII imprimible) y tipo de dispositivo (0 = disco). */
void msc_parse_inquiry(const uint8_t d[36], char vendor[9], char product[17], uint8_t *devtype);
#endif
