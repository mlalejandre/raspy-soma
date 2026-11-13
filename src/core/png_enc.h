#ifndef SOMA_PNG_ENC_H
#define SOMA_PNG_ENC_H
/* PARCHE 083: codificador PNG (RGB de 8 bits) con compresion deflate de verdad (LZ77 con cadenas hash + Huffman fijo) y filtro por fila
 * (None/Sub/Up, el que menos suma). Un dibujo de 320x240 ocupa unos pocos KiB: cabe en RamFS (16 KiB por archivo) y se abre en
 * cualquier sistema. Sin memoria dinamica: quien llama da el buffer de salida y uno de trabajo (png_work_size). Pruebas: tests/test_png.c. */
#include <stdint.h>
#include <stddef.h>
size_t png_work_size(int w, int h);
/* argb: w*h pixeles (se ignora el alfa). Devuelve los bytes escritos en out o 0 si no cabe en cap. */
size_t png_encode_rgb(const uint32_t *argb, int w, int h, uint8_t *out, size_t cap, void *work);
/* Piezas sueltas (tambien probadas): */
uint32_t png_crc32(uint32_t crc, const uint8_t *p, size_t n);
uint32_t png_adler32(const uint8_t *p, size_t n);
/* zlib (cabecera 78 01 + bloques deflate + adler32). Devuelve bytes o 0 si no cabe. work >= deflate_work_size(n). */
size_t deflate_work_size(size_t n);
size_t zlib_compress(const uint8_t *in, size_t n, uint8_t *out, size_t cap, void *work);
#endif
