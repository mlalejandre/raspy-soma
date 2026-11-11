#ifndef SOMA_WEB_INFLATE_H
#define SOMA_WEB_INFLATE_H
#include "web_util.h"
/* fmt: 0 deflate crudo, 1 zlib, 2 gzip, 3 autodeteccion (gzip/zlib/crudo). Anade a `out`. 0 = OK, <0 = error. */
int wz_inflate(const uint8_t *in, uint32_t n, int fmt, struct wb *out, uint32_t max_out);
#endif
