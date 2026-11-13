#ifndef SOMA_CODER_CORE_H
#define SOMA_CODER_CORE_H
/* PARCHE 096: nucleo agentico del programador de SOMA (independiente de la interfaz; se prueba en el host con un LLM simulado).
 *
 * Bucle generar -> normalizar -> verificar -> reparar, guiado por el compilador:
 *  - Contexto anclado: ficha de referencia de SOMA-LANG sacada del compilador real + ejemplo verificado que se elige segun la tarea
 *    (ventana o consola) + lecciones aprendidas de trabajos anteriores + codigo actual completo si es una modificacion.
 *  - Salida en bloque ```soma``` (sin JSON ni comillas escapadas) y extraccion robusta (<think>, prosa, bloques sin cerrar).
 *  - Normalizador determinista: corrige sin gastar llamadas los fallos tipicos de un LLM (Python/JavaScript: elif, True, x++, var,
 *    function, console.log, .length, #, comillas tipograficas, el "manejador" de ventana que SOMA-LANG no usa...).
 *  - Verificacion: compilador + lint + comprobaciones semanticas de la tarea (pide ventana y no la crea, falta win.present,
 *    modificacion que no cambia nada, programa cortado).
 *  - Reparacion con el error exacto y las lineas de alrededor numeradas, pista especifica por tipo de error y el archivo completo.
 *  - Mejor candidato conservado; si dos rondas no mejoran se regenera desde cero con un enfoque mas simple; presupuesto de llamadas.
 *  - Revision critica final (una ronda) solo si queda presupuesto; el resultado revisado se acepta solo si no empeora.
 *  - El archivo se escribe UNA vez y solo si el resultado compila: un fallo nunca deja el disco a medias.
 *  - Lecciones: cada error reparado se guarda en /agent/coder_lessons.txt y se ofrece en los siguientes trabajos. */
#include <stdint.h>

#define CC_SRC_MAX    32768
#define CC_LESSONS    "/agent/coder_lessons.txt"

struct cc_env {
    int      (*llm)(const char *prompt, char *out, uint32_t max, uint32_t timeout_ms);   /* 1 = respuesta */
    int      (*read)(const char *path, char *buf, uint32_t max);                         /* bytes o <0 */
    int      (*write)(const char *path, const char *buf, uint32_t len);                  /* >=0 ok */
    void     (*log)(const char *msg);
    void     (*status)(int phase, const char *desc, const char *thought);
    int      (*cancelled)(void);
    int      max_llm_calls;                                                              /* 0 = 7 */
    void     (*trace)(const char *kind, const char *prompt, const char *reply);              /* PARCHE 097: opcional */
    int      force_new;      /* PARCHE 098: crear desde cero aunque el archivo exista (no es una modificacion) */
};

struct cc_result {
    int      ok;               /* el archivo se escribio y compila */
    int      wrote;
    int      llm_calls;
    int      repairs;
    int      regenerations;
    int      auto_fixes;       /* correcciones deterministas aplicadas */
    int      reviewed;         /* hubo revision critica */
    int      cancelled;
    int      stuck;            /* el modelo repetia la misma respuesta */
    int      score;            /* 0 = perfecto */
    uint32_t bc_len;
    char     summary[192];
    char     last_error[192];
    char     plan[480];
};

int      cc_run(const struct cc_env *env, const char *target, const char *mission, struct cc_result *res);
/* piezas sueltas (pruebas) */
uint32_t cc_extract_code(const char *reply, char *out, uint32_t cap);
int      cc_normalize(char *src, uint32_t *len, uint32_t cap);   /* devuelve el numero de correcciones */
int      cc_mission_is_gui(const char *mission);
int      cc_busy(void);
#endif
