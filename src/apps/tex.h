#ifndef SOMA_TEX_H
#define SOMA_TEX_H
/* PARCHE 084: motor LaTeX de SOMA (subconjunto practico de LaTeX + matematicas al estilo TeX).
 *
 * Entrada: el texto de un .tex (UTF-8). Salida: una lista de operaciones de dibujo (texto en una posicion, tamano y estilo; rectangulos;
 * lineas; imagenes) con coordenadas de pagina, lista para pintar en cualquier sitio. No dibuja ni reserva memoria: usa tablas estaticas
 * y pide las medidas del texto a quien llama (tex_metrics), asi que se prueba en el host con medidas falsas (tests/test_tex.c).
 *
 * Documento: \documentclass, \usepackage (se ignoran), \title \author \date \maketitle, \begin{abstract}, \section \subsection
 * \subsubsection (y con *), \paragraph, numeracion 1, 1.1, 1.1.1, \label y \ref, itemize / enumerate / description anidados, quote,
 * center, verbatim, tabular (l c r |, \hline, &, \\), figure + \includegraphics + \caption, \footnote, \textbf \textit \emph \texttt
 * \underline \textsc \textcolor \href \url, tamanos \small \large \Large \huge, \today, acentos (\'a \~n \"u ...), --, ---, comillas,
 * caracteres escapados, comentarios con %, \newline \\ \par \hrule \noindent \vspace \newpage.
 * Matematicas: $..$, \(..\), $$..$$, \[..\], equation (numerada), equation*, align (& alinea, \\ separa, numeradas), \frac \dfrac \tfrac
 * \binom \sqrt[n]{} ^ _ (con prioridades y {}), \sum \prod \int \oint \lim \max \min ... con limites, \left \right con delimitadores que
 * crecen, matrices (matrix, pmatrix, bmatrix, vmatrix, cases), acentos (\hat \bar \vec \dot \tilde \overline), \text \mathrm \mathbf
 * \mathbb \mathcal, letras griegas, operadores y relaciones con su espaciado TeX, \sin \cos \log ..., espacios \, \; \quad \qquad. */
#include <stdint.h>

struct tex_metrics {
    int (*width)(const char *s, int n, int px, int style, void *u);   /* ancho en px del texto UTF-8 */
    int (*ascent)(int px, void *u);
    int (*descent)(int px, void *u);
    int (*image)(const char *path, int *w, int *h, void *u);          /* 0 = existe; puede ser NULL */
    void *u;
};

enum { TS_BOLD = 1, TS_ITALIC = 2, TS_MONO = 4, TS_UNDER = 8 };
enum { TOP_TEXT = 0, TOP_RECT, TOP_LINE, TOP_IMAGE };
struct tex_op {
    uint8_t type, style; int16_t px;      /* TEXT: tamano en px y estilo; LINE: grosor en 1/16 px; RECT con style 1: fondo (pintarlo antes) */
    int32_t x, y, w, h;                   /* TEXT: x, y = linea base; RECT: caja; LINE: de (x,y) a (x+w,y+h) en 1/16 px; IMAGE: caja */
    uint32_t rgb;
    uint32_t s, n;                        /* TEXT: texto en el pool; IMAGE: ruta en el pool */
};
struct tex_out {
    struct tex_op *ops; int nops, cap;
    char *pool; uint32_t npool, poolcap;
    int height;                           /* alto total del documento en px */
    char title[96];
    int errors;                           /* construcciones desconocidas (se muestran en rojo) */
    int sections, equations, footnotes;
};

/* Maqueta src en una columna de 'width' px con letra base de base_px. Devuelve 0 o -1 si algo se lleno (lo que cupo vale). */
int  tex_typeset(const char *src, uint32_t len, int width, int base_px, const struct tex_metrics *m, struct tex_out *o);
/* 1 si el texto parece LaTeX (\documentclass, \section, \begin{...}, $...$ o \frac). */
int  tex_is_latex(const char *src, uint32_t len);
/* Colores del tema "papel" */
#define TEX_INK     0x001F2937u
#define TEX_HEAD    0x000F172Au
#define TEX_MUTED   0x006B7280u
#define TEX_LINK    0x001D4ED8u
#define TEX_ACCENT  0x000EA5E9u
#define TEX_RULE    0x00D1D5DBu
#define TEX_CODEBG  0x00F3F4F6u
#define TEX_ERR     0x00DC2626u
#endif
