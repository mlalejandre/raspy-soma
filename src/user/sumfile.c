/* sumfile <ruta>: lee un archivo del almacen BIGFS en streaming y calcula su FNV-1a 64 (comprobable en el host).
 * Tambien comprueba que el kernel rechaza punteros invalidos en las llamadas de ficheros. */
#include "ulib.h"

static unsigned char buf[16384];

/* La linea final se compone en un bufer y se escribe de UNA vez: dos procesos a la vez no entrelazan su salida */
static char line[200];
static int ln;
static void cat(const char *s) { while (*s && ln < (int)sizeof line - 1) line[ln++] = *s++; }
static void catdec(u64 v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } cat(b + i); }
static void cathex64(u64 v)
{
    static const char H[] = "0123456789abcdef";
    char b[17]; for (int i = 15; i >= 0; --i) { b[i] = H[v & 15]; v >>= 4; } b[16] = 0;
    cat(b);
}

void _start(void)
{
    char path[128];
    i64 n = u_getargs(path, sizeof path);
    if (n <= 0) { u_puts("[sumfile] uso: exec /bin/sumfile.elf <ruta del almacen>\n"); u_exit(2); }

    /* punteros invalidos: se rechazan, no tumban nada */
    i64 r = u_sys3(SYS_OPEN, 0x100000, 8, 0);                       /* ruta en memoria del kernel */
    u_puts(r < 0 ? "[sumfile] ruta de kernel rechazada\n" : "[sumfile] ERROR: ruta de kernel aceptada\n");
    if (r >= 0) u_exit(9);

    i64 fd = u_open(path);
    if (fd < 0) { u_puts("[sumfile] no se puede abrir: -"); u_putdec((u64)(-fd)); u_puts("\n"); u_exit(3); }
    r = u_read(fd, (void *)0x100000, 16);                           /* destino en memoria del kernel */
    u_puts(r < 0 ? "[sumfile] lectura a memoria del kernel rechazada\n" : "[sumfile] ERROR: lectura a kernel aceptada\n");
    if (r >= 0) u_exit(9);
    if (u_read(99, buf, 16) >= 0 || u_close(99) >= 0) { u_puts("[sumfile] ERROR: descriptor invalido aceptado\n"); u_exit(9); }

    i64 size = u_fsize(fd);
    u64 h = 1469598103934665603ULL, total = 0;
    for (;;) {
        i64 g = u_read(fd, buf, sizeof buf);
        if (g < 0) { u_puts("[sumfile] error de lectura\n"); u_exit(4); }
        if (g == 0) break;
        for (i64 i = 0; i < g; ++i) { h ^= buf[i]; h *= 1099511628211ULL; }
        total += (u64)g;
    }
    /* seek: al final == tamano; al principio y releer 16 bytes da lo mismo que la primera lectura */
    i64 e1 = u_seek(fd, 0, 2), e2 = u_seek(fd, 0, 0);
    i64 e3 = u_seek(fd, -1, 0);                                     /* posicion negativa: invalida */
    int seek_ok = e1 == size && e2 == 0 && e3 < 0;
    u_close(fd);
    cat("[sumfile] "); cat(path); cat(" bytes="); catdec(total); cat(" size="); catdec((u64)size);
    cat(" fnv="); cathex64(h); cat(seek_ok ? " seek=ok\n" : " seek=ERROR\n");
    u_write(line, (u64)ln);
    u_exit(total == (u64)size && seek_ok ? 0 : 1);
}
