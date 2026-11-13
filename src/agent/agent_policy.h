/* PARCHE 110 (Fase 1, permisos): politica de escritura de los AGENTES sobre el VFS, como funcion pura para poder probarla en host.
 * Un agente (LLM o autonomo) NO puede crear, modificar, borrar ni mover nada bajo /etc/ (arranque y configuracion), /sys/ (estado del
 * sistema), /bin/ ni /boot/. Antes solo se protegian dos archivos concretos y SOLO en el comando `write`: `rm`, `cp`, `mv`, `touch`,
 * la redireccion `>` y `descargar` (contenido de la red) podian reescribir /etc/init.sh y dejar codigo persistente en el arranque.
 * La ruta se normaliza (//, /./, /../) antes de decidir, y una ruta demasiado larga para el VFS se trata como protegida. */
#ifndef SOMA_AGENT_POLICY_H
#define SOMA_AGENT_POLICY_H
#include <stdint.h>

#define AGENT_POLICY_PATH_MAX 255

/* Normaliza `path` en `out` (cap >= 2): siempre empieza por '/', sin '//', '.' ni '..'. Devuelve 0 si no cabe. */
static inline int agent_path_canon(const char *path, char *out, unsigned cap)
{
    unsigned o = 0;
    const char *p = path;
    if (!p || cap < 2) return 0;
    out[o++] = '/';
    while (*p) {
        while (*p == '/') p++;
        const char *s = p;
        while (*p && *p != '/') p++;
        unsigned n = (unsigned)(p - s);
        if (n == 0) break;
        if (n == 1 && s[0] == '.') continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {                  /* sube un nivel (nunca por encima de la raiz) */
            if (o > 1) { o--; while (o > 1 && out[o - 1] != '/') o--; if (o > 1) o--; }
            continue;
        }
        if (o > 1) { if (o + 1 >= cap) return 0; out[o++] = '/'; }
        if (o + n >= cap) return 0;
        for (unsigned i = 0; i < n; ++i) out[o++] = s[i];
    }
    out[o] = '\0';
    return 1;
}

static inline int agent_path_has_prefix(const char *c, const char *dir)
{
    unsigned i = 0;
    while (dir[i] && c[i] == dir[i]) i++;
    return dir[i] == '\0' && (c[i] == '\0' || c[i] == '/');
}

/* 1 si los agentes NO pueden modificar `path`. */
static inline int agent_path_protected(const char *path)
{
    static const char *const dirs[] = { "/etc", "/sys", "/bin", "/boot", 0 };
    char c[AGENT_POLICY_PATH_MAX + 1];
    if (!path || !agent_path_canon(path, c, sizeof c)) return 1;
    for (int i = 0; dirs[i]; ++i)
        if (agent_path_has_prefix(c, dirs[i])) return 1;
    return 0;
}
#endif
