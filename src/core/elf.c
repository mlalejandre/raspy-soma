/* SOMA_PARCHE_016_EXEC: ver elf.h */
#include "elf.h"

#define PT_LOAD    1u
#define PT_DYNAMIC 2u
#define PT_INTERP  3u
#define ET_EXEC    2u
#define EM_X86_64  62u
#define PAGE       4096ULL

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

const char *elf_strerror(int c)
{
    switch (c) {
    case ELF_OK: return "ok";
    case ELF_E_SIZE: return "archivo demasiado corto";
    case ELF_E_MAGIC: return "no es un ELF (firma incorrecta)";
    case ELF_E_FORMAT: return "formato no soportado (se exige ELF64 little-endian, ET_EXEC, x86-64)";
    case ELF_E_PHDR: return "tabla de cabeceras de programa invalida";
    case ELF_E_UNSUPPORTED: return "enlazado dinamico/interprete no soportado";
    case ELF_E_SEGMENT: return "segmento fuera del archivo o de la ventana de usuario";
    case ELF_E_WX: return "segmento escribible y ejecutable a la vez (W^X)";
    case ELF_E_OVERLAP: return "segmentos que comparten pagina";
    case ELF_E_TOOBIG: return "programa demasiado grande";
    case ELF_E_ENTRY: return "punto de entrada fuera del codigo";
    case ELF_E_NOLOAD: return "sin segmentos cargables";
    default: return "error desconocido";
    }
}

int elf_parse(const uint8_t *img, size_t len, const struct elf_limits *lim, struct elf_image *out)
{
    if (!img || !lim || !out) return ELF_E_SIZE;
    out->entry = 0; out->nsegs = 0;
    if (len < 64) return ELF_E_SIZE;
    if (img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F') return ELF_E_MAGIC;
    if (img[4] != 2 /*64 bits*/ || img[5] != 1 /*little endian*/ || img[6] != 1 /*version*/) return ELF_E_FORMAT;
    if (rd16(img + 16) != ET_EXEC || rd16(img + 18) != EM_X86_64) return ELF_E_FORMAT;
    if (rd32(img + 20) != 1) return ELF_E_FORMAT;

    uint64_t entry = rd64(img + 24);
    uint64_t phoff = rd64(img + 32);
    uint16_t phentsize = rd16(img + 54);
    uint16_t phnum = rd16(img + 56);
    if (phentsize != 56 || phnum == 0 || phnum > 16) return ELF_E_PHDR;
    uint64_t ph_bytes = (uint64_t)phnum * 56u;
    if (phoff > len || ph_bytes > len - phoff) return ELF_E_PHDR;

    uint64_t total_pages = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        const uint8_t *ph = img + phoff + (uint64_t)i * 56u;
        uint32_t type = rd32(ph);
        if (type == PT_INTERP || type == PT_DYNAMIC) return ELF_E_UNSUPPORTED;
        if (type != PT_LOAD) continue;

        uint32_t flags  = rd32(ph + 4);
        uint64_t offset = rd64(ph + 8);
        uint64_t vaddr  = rd64(ph + 16);
        uint64_t filesz = rd64(ph + 32);
        uint64_t memsz  = rd64(ph + 40);

        if (memsz == 0) {                       /* PT_LOAD vacio (ld lo emite si no hay datos): se ignora */
            if (filesz != 0) return ELF_E_SEGMENT;
            continue;
        }
        if (out->nsegs >= ELF_MAX_SEGS) return ELF_E_TOOBIG;
        if (filesz > memsz) return ELF_E_SEGMENT;
        if (offset > len || filesz > len - offset) return ELF_E_SEGMENT;
        if (vaddr < lim->base || vaddr - lim->base > lim->area || memsz > lim->area - (vaddr - lim->base)) return ELF_E_SEGMENT;
        if ((flags & ELF_PF_W) && (flags & ELF_PF_X)) return ELF_E_WX;

        uint64_t first = vaddr / PAGE, last = (vaddr + memsz - 1) / PAGE;   /* sin desborde: vaddr+memsz <= base+area */
        for (int k = 0; k < out->nsegs; ++k) {
            uint64_t f2 = out->seg[k].vaddr / PAGE, l2 = (out->seg[k].vaddr + out->seg[k].memsz - 1) / PAGE;
            if (first <= l2 && f2 <= last) return ELF_E_OVERLAP;
        }
        total_pages += last - first + 1;
        if (total_pages > lim->max_pages) return ELF_E_TOOBIG;

        struct elf_seg *s = &out->seg[out->nsegs++];
        s->vaddr = vaddr; s->memsz = memsz; s->filesz = filesz; s->offset = offset; s->flags = flags;
    }
    if (out->nsegs == 0) return ELF_E_NOLOAD;

    int ok = 0;
    for (int k = 0; k < out->nsegs; ++k) {
        const struct elf_seg *s = &out->seg[k];
        if ((s->flags & ELF_PF_X) && entry >= s->vaddr && entry - s->vaddr < s->memsz) ok = 1;
    }
    if (!ok) return ELF_E_ENTRY;
    out->entry = entry;
    return ELF_OK;
}
