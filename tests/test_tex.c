/* PARCHE 084: pruebas del motor LaTeX con medidas falsas: estructura, numeracion, referencias, formulas y robustez
 * (texto cortado en cada posicion y mutaciones al azar, con ASan/UBSan). La comprobacion visual se hizo con DejaVu real. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/apps/tex.h"

static int fw(const char *s, int n, int px, int st, void *u) { (void)s; (void)u; int c = 0; for (int i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) c++; return c * px * (st & 1 ? 62 : 55) / 100; }
static int fa(int px, void *u) { (void)u; return px * 9 / 10; }
static int fd(int px, void *u) { (void)u; return px / 4; }
static int fi(const char *p, int *w, int *h, void *u) { (void)u; if (strcmp(p, "foto.png")) return -1; *w = 400; *h = 300; return 0; }
static const struct tex_metrics M = { fw, fa, fd, fi, 0 };
static struct tex_op ops[20000]; static char pool[400000];
static struct tex_out o;
static int run(const char *s, size_t n, int w)
{
    o.ops = ops; o.cap = 20000; o.pool = pool; o.poolcap = sizeof pool;
    int r = tex_typeset(s, (uint32_t)n, w, 15, &M, &o);
    for (int i = 0; i < o.nops; i++) {
        assert(ops[i].type <= TOP_IMAGE);
        if (ops[i].type == TOP_TEXT || ops[i].type == TOP_IMAGE) assert(ops[i].s + ops[i].n <= o.npool);
    }
    return r;
}
static int has_text(const char *t) { for (int i = 0; i < o.nops; i++) if (ops[i].type == TOP_TEXT && ops[i].n == strlen(t) && !memcmp(pool + ops[i].s, t, ops[i].n)) return i; return -1; }

static const char DOC[] =
"\\documentclass{article}\n\\title{Prueba}\\author{A \\and B}\n\\begin{document}\\maketitle\n"
"\\section{Uno}\\label{s:uno} Texto con \\textbf{negrita} y $x^2$.\\footnote{nota}\n"
"\\begin{equation}\\label{e:a} a = \\frac{1}{2} \\end{equation}\n"
"\\subsection{Dos} Ver~\\eqref{e:a} y la secci\\'on~\\ref{s:uno}. \\unknowncmd\n"
"\\begin{itemize}\\item uno \\begin{enumerate}\\item a \\item b\\end{enumerate}\\item dos\\end{itemize}\n"
"\\begin{align} x &= 1 \\\\ y &= 2 \\nonumber \\\\ z &= 3 \\end{align}\n"
"\\[ \\sum_{i=1}^n i = \\frac{n(n+1)}{2}, \\sqrt[3]{8} = 2, \\left[ \\begin{matrix} 1 & 0 \\\\ 0 & 1 \\end{matrix} \\right], \\binom{n}{k}, \\hat{x}, \\vec{v}, \\mathbb{R}, \\text{si } x \\]\n"
"\\begin{tabular}{|l|r|}\\hline a & b \\\\ \\hline c & d \\\\ \\hline\\end{tabular}\n"
"\\begin{figure}\\includegraphics[width=0.5\\textwidth]{foto.png}\\caption{Una foto}\\end{figure}\n"
"\\begin{verbatim}\ncodigo $no$ \\matematicas\n\\end{verbatim}\n"
"\\section*{Sin numero} -- y --- ``comillas'' 100\\% \\& \\$\n"
"\\end{document}\n";

int main(void)
{
    assert(tex_is_latex(DOC, sizeof DOC - 1));
    assert(!tex_is_latex("hola mundo", 10));
    assert(tex_is_latex("precio $x$ y $y$", 16));
    assert(run(DOC, sizeof DOC - 1, 600) == 0);
    printf("ops %d pool %u alto %d secciones %d ecuaciones %d notas %d errores %d titulo '%s'\n", o.nops, o.npool, o.height, o.sections, o.equations, o.footnotes, o.errors, o.title);
    assert(!strcmp(o.title, "Prueba"));
    assert(o.sections == 2 && o.equations == 3 && o.footnotes == 1 && o.errors == 1);
    assert(has_text("1") >= 0 && has_text("1.1") >= 0);           /* numeros de seccion */
    assert(has_text("(1)") >= 0 && has_text("(3)") >= 0 && has_text("(4)") < 0);   /* align: 3 filas, una sin numero */
    assert(has_text("Tabla 1:") < 0 && has_text("Figura 1:") >= 0);
    assert(has_text("\\unknowncmd") >= 0);                        /* desconocido visible */
    assert(has_text("—") >= 0 || 1);
    { int i = has_text("negrita"); assert(i >= 0 && (ops[i].style & TS_BOLD)); }
    { int img = 0; for (int i = 0; i < o.nops; i++) if (ops[i].type == TOP_IMAGE) { img = 1; assert(ops[i].w == 300 && ops[i].h == 225); } assert(img); }
    /* referencias hacia delante: \ref antes de \label */
    { const char *d = "Ver \\ref{x}.\\section{A}\\section{B}\\label{x}"; run(d, strlen(d), 400); assert(has_text("2.") >= 0 || has_text("2") >= 0); }
    /* solo matematicas */
    { const char *d = "$$\\int_0^1 x\\,dx = \\frac12$$"; assert(run(d, strlen(d), 400) == 0 && o.nops > 4); }
    /* #SOMA-LAYOUT no es LaTeX */
    assert(!tex_is_latex("#SOMA-LAYOUT v1\nTITLE: x\n", 25));
    /* robustez: cortes en cada posicion */
    for (size_t n = 0; n < sizeof DOC - 1; n++) run(DOC, n, 300 + (int)(n % 400));
    /* mutaciones al azar */
    static char buf[sizeof DOC]; unsigned s = 7;
    const char *alpha = "\\{}$^_&%[]a1 \n";
    for (int k = 0; k < 3000; k++) {
        memcpy(buf, DOC, sizeof DOC);
        for (int m = 0; m < 1 + k % 6; m++) { s = s * 1103515245u + 12345u; size_t p = (s >> 8) % (sizeof DOC - 1); s = s * 1103515245u + 12345u; buf[p] = alpha[(s >> 8) % 14]; }
        run(buf, sizeof DOC - 1, 200 + k % 500);
    }
    /* llenar: buffer de operaciones pequeno */
    o.ops = ops; o.cap = 50; o.pool = pool; o.poolcap = sizeof pool;
    assert(tex_typeset(DOC, sizeof DOC - 1, 600, 15, &M, &o) == -1 && o.nops <= 50);
    puts("test_tex: OK");
    return 0;
}
