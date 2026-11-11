#ifndef MYOS_ELF_H
#define MYOS_ELF_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_016_EXEC: validador de ejecutables ELF64 estaticos (ET_EXEC, x86-64).
 * No carga nada: comprueba TODO el archivo con aritmetica sin desbordes y devuelve los segmentos
 * que hay que mapear. Es codigo neutro de arquitectura y se prueba en host (tests/test_elf.c). */
#define ELF_MAX_SEGS 4

struct elf_limits {
    uint64_t base;          /* primera direccion virtual permitida */
    uint64_t area;          /* bytes de ventana a partir de base */
    uint32_t max_pages;     /* total de paginas de 4 KiB que pueden ocupar los segmentos */
};

#define ELF_PF_X 1u
#define ELF_PF_W 2u
#define ELF_PF_R 4u

struct elf_seg {
    uint64_t vaddr, memsz, filesz, offset;
    uint32_t flags;
};

struct elf_image {
    uint64_t entry;
    int nsegs;
    struct elf_seg seg[ELF_MAX_SEGS];
};

#define ELF_OK            0
#define ELF_E_SIZE       (-1)   /* demasiado corto para su cabecera */
#define ELF_E_MAGIC      (-2)
#define ELF_E_FORMAT     (-3)   /* no es ELF64 little-endian ET_EXEC x86-64 */
#define ELF_E_PHDR       (-4)   /* tabla de cabeceras de programa fuera del archivo / tamano incorrecto */
#define ELF_E_UNSUPPORTED (-5)  /* enlazado dinamico / interprete */
#define ELF_E_SEGMENT    (-6)   /* segmento fuera del archivo, de la ventana o inconsistente */
#define ELF_E_WX         (-7)   /* segmento escribible y ejecutable a la vez */
#define ELF_E_OVERLAP    (-8)   /* dos segmentos comparten pagina */
#define ELF_E_TOOBIG     (-9)   /* mas paginas de las permitidas */
#define ELF_E_ENTRY      (-10)  /* el punto de entrada no cae en un segmento ejecutable */
#define ELF_E_NOLOAD     (-11)  /* ningun segmento PT_LOAD */

int elf_parse(const uint8_t *img, size_t len, const struct elf_limits *lim, struct elf_image *out);
const char *elf_strerror(int code);

#endif
