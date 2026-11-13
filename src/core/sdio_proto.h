#ifndef SOMA_SDIO_PROTO_H
#define SOMA_SDIO_PROTO_H
/* PARCHE 079: partes puras del Wi-Fi de la Pi 5 (chip Cypress/Broadcom CYW43455 por SDIO), sin acceso a hardware y probadas en el host
 * (tests/test_sdio.c): argumentos de CMD52/CMD53, respuesta R5, tuplas CIS, identificador del chip, tabla de nucleos (EROM) del bus
 * interno AXI, empaquetado del fichero NVRAM y la estructura compartida que publica el firmware al arrancar.
 * Referencia de comportamiento: especificacion SDIO simplificada y el driver brcmfmac de Linux (reescrito, sin copiar codigo). */
#include <stdint.h>

/* ---- SDIO ---- */
uint32_t sdio_cmd52_arg(int write, unsigned fn, uint32_t addr, uint8_t data, int raw);
/* CMD53: block = modo bloque, incr = direccion incremental; count 1..512 (512 se codifica como 0). 0 = argumento no valido. */
uint32_t sdio_cmd53_arg(int write, unsigned fn, int block, int incr, uint32_t addr, uint32_t count);
/* Respuesta R5 (CMD52/53): 1 si no hay errores (CRC, orden ilegal, error general, funcion invalida, fuera de rango). */
int sdio_r5_ok(uint32_t r5);
/* R4 (CMD5): listo (bit 31), numero de funciones de E/S (bits 30:28) y OCR (23:0). */
#define SDIO_R4_READY(r) (((r) >> 31) & 1u)
#define SDIO_R4_NFUNC(r) (((r) >> 28) & 7u)
#define SDIO_R4_OCR(r)   ((r) & 0x00FFFFFFu)
/* Recorre tuplas CIS (len bytes). Devuelve 0 y el fabricante/tarjeta de la tupla MANFID (0x20), -1 si no esta o esta mal formada. */
int sdio_cis_manfid(const uint8_t *cis, uint32_t len, uint16_t *manf, uint16_t *card);

/* ---- Registros de la funcion 1 del chip Broadcom (ventana al bus interno) ---- */
#define SB_F1_SBADDRLOW   0x1000Au
#define SB_F1_SBADDRMID   0x1000Bu
#define SB_F1_SBADDRHIGH  0x1000Cu
#define SB_F1_CHIPCLKCSR  0x1000Eu
#define SB_F1_SDIOPULLUP  0x1000Fu
#define SB_WIN_MASK       0x7FFFu         /* ventana de 32 KiB */
#define SB_ACCESS_4B      0x8000u         /* bit para accesos de 32 bits dentro de la ventana */
#define CLK_FORCE_ALP     0x01u
#define CLK_FORCE_HT      0x02u
#define CLK_ALP_REQ       0x08u
#define CLK_HT_REQ        0x10u
#define CLK_HW_REQ_OFF    0x20u
#define CLK_ALP_AVAIL     0x40u
#define CLK_HT_AVAIL      0x80u
/* Bytes de la ventana (low, mid, high) para una direccion del bus interno. */
void sb_window(uint32_t addr, uint8_t out[3]);

/* ---- Chip y nucleos ---- */
#define SI_ENUM_BASE      0x18000000u
#define CC_EROMPTR        0xFCu
struct brcm_chipid { uint16_t id; uint8_t rev, pkg, ncores, socitype; };
void brcm_chipid_decode(uint32_t v, struct brcm_chipid *o);
#define CORE_CHIPCOMMON 0x800
#define CORE_SOCRAM     0x80E
#define CORE_D11        0x812
#define CORE_PMU        0x827
#define CORE_SDIO_DEV   0x829
#define CORE_ARM_CR4    0x83E
struct brcm_core { uint16_t id; uint8_t rev; uint32_t base, wrap; };
/* Interpreta la EROM (palabras de 32 bits) hasta la marca de fin. Devuelve los nucleos escritos (<= max) o -1 si esta mal formada. */
int brcm_erom_parse(const uint32_t *w, unsigned nwords, struct brcm_core *out, int max);
const char *brcm_core_name(uint16_t id);
/* Envoltorio AI (agente del nucleo) */
#define AI_IOCTL        0x408u
#define AI_RESETCTL     0x800u
#define AI_RESETST      0x804u
#define AI_IOCTL_CLK    0x1u
#define AI_IOCTL_FGC    0x2u
#define CR4_IOCTL_HALT  0x20u
#define D11_IOCTL_PHYCLK 0x4u
#define D11_IOCTL_PHYRST 0x8u
/* Registros del nucleo ARM CR4 para medir la RAM */
#define CR4_CAP         0x04u
#define CR4_BANKIDX     0x40u
#define CR4_BANKINFO    0x44u
uint32_t brcm_cr4_bank_bytes(uint32_t bankinfo);
/* Base de la RAM del procesador del chip (rambase) segun el modelo; 0 = desconocido. */
uint32_t brcm_rambase(uint16_t chip);

/* ---- NVRAM ---- */
/* Convierte el .txt (clave=valor por linea, '#' comentario) al formato binario que espera el firmware: entradas separadas por NUL, NUL
 * final, relleno a 4 bytes y palabra final con la longitud en palabras y su complemento. Devuelve los bytes escritos o -1 si no cabe. */
int brcm_nvram_pack(const char *txt, uint32_t len, uint8_t *out, uint32_t max);
/* 1 si la ultima palabra de la RAM todavia es la marca de la NVRAM (el firmware no ha arrancado). */
int brcm_is_nvram_token(uint32_t v);

/* ---- Estructura compartida (sdpcm_shared) ---- */
struct brcm_shared { uint32_t flags, trap_addr, assert_exp, assert_file, assert_line, console_addr, msgtrace; };
#define SHARED_VERSION_MASK 0xFFu
#define SHARED_ASSERT       0x200u
#define SHARED_TRAP         0x400u
/* 0 = bien: estructura leida (al menos 7 palabras little-endian) y version 1..4. */
int brcm_shared_parse(const uint8_t *b, uint32_t len, struct brcm_shared *o);

/* ---- Placa (codigo de revision del firmware de la Raspberry Pi) ---- */
struct rpi_board { int new_style; uint32_t type, mem_mb, rev, maker; const char *name; int has_wifi; };
void rpi_board_decode(uint32_t rev, struct rpi_board *o);
#endif
