#ifndef SOMA_SAVEDLG_H
#define SOMA_SAVEDLG_H
/* PARCHE 085: dialogo "Guardar imagen como" reutilizable (de momento lo usa el navegador). Modal dentro de la ventana que lo abre:
 * nombre, carpeta (escrita o elegida entre las que hay en RamFS y el almacen BIGFS si existe), formato PNG/BMP y tamano 100-25 %,
 * con el peso estimado y si cabe en RamFS (16 KiB por archivo). */
#include <stdint.h>
void savedlg_open(const uint32_t *argb, int w, int h, const char *suggest_name);   /* copia los pixeles */
int  savedlg_active(void);
void savedlg_render(int wx, int wy, int ww, int wh);
int  savedlg_click(int32_t mx, int32_t my, int wx, int wy, int ww, int wh);      /* 1 = consumido */
int  savedlg_key(char c);                                                         /* 1 = consumido */
#endif
