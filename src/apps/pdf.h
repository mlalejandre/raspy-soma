#ifndef SOMA_PDF_H
#define SOMA_PDF_H
/* PARCHE 088: lector de PDF propio (solo enteros, sin coma flotante: el nucleo compila con -mgeneral-regs-only).
 * Lee la tabla xref clasica o en flujo (PDF 1.5), flujos de objetos, FlateDecode con predictores, DCTDecode (JPEG) e imagenes en
 * crudo; recorre el arbol de paginas y convierte el contenido de cada pagina en operaciones de dibujo en pixeles: palabras de texto
 * (colocadas con los anchos del propio PDF; Unicode por ToUnicode, WinAnsi, MacRoman o /Differences), rellenos y trazos de trayectos
 * (curvas aplanadas) e imagenes. No dibuja las fuentes incrustadas: el texto sale con DejaVu en su sitio y tamano. Sin cifrado. */
#include <stdint.h>

enum { PDF_OP_TEXT = 1, PDF_OP_PATH, PDF_OP_IMAGE };
#define PDF_TF_BOLD   1
#define PDF_TF_ITALIC 2
#define PDF_TF_MONO   4
#define PDF_TF_SERIF  8

struct pdf_op {
    uint8_t kind;
    uint8_t flags;            /* texto: PDF_TF_*; trayecto: 1 relleno, 2 trazo, 4 par-impar */
    int16_t size;             /* texto: alto de letra en px; trayecto: grosor del trazo en 1/16 px */
    int32_t x, y, w, h;       /* texto: x, linea base y ancho esperado (px). Imagen: rectangulo. Trayecto: caja */
    uint32_t color;           /* 0xRRGGBB (trazo para PATH con flags&2; relleno en fill) */
    uint32_t fill;            /* PATH: color de relleno */
    const char *text; int len;            /* TEXT: UTF-8 */
    const int32_t *pts; int npts;         /* PATH: x,y en 1/16 px; un -1,-1 (en ambos) separa subtrayectos */
    const uint32_t *img; int iw, ih;      /* IMAGE: ARGB */
    uint8_t flip_x, flip_y;
    uint8_t font;             /* TEXT: fuente (1..64) para igualar la escala de todas sus palabras */
};
struct pdf_page {
    struct pdf_op *ops; int nops, cap;
    int w, h;                 /* tamano de la pagina en px */
    int unknown;              /* operadores/filtros que no se entendieron */
    void *arena;
};

struct pdf;
struct pdf *pdf_open(const uint8_t *data, uint32_t len, char *err, int errmax);   /* data debe seguir vivo hasta pdf_close */
void pdf_close(struct pdf *p);
int  pdf_page_count(const struct pdf *p);
int  pdf_page_size(struct pdf *p, int idx, int *w_pt, int *h_pt);   /* puntos (1/72") */
int  pdf_title(struct pdf *p, char *out, int max);                   /* /Info /Title en UTF-8 (0 si no hay) */
/* escala: px por punto * 256 (256 = 72 ppp). 0 = OK */
int  pdf_render_page(struct pdf *p, int idx, int scale256, struct pdf_page *out);
void pdf_page_free(struct pdf_page *pg);
/* texto de la pagina (para buscar/copiar): UTF-8 en out, devuelve longitud */
int  pdf_page_text(const struct pdf_page *pg, char *out, int max);

#endif
