#ifndef SOMA_CODER_DEMO_H
#define SOMA_CODER_DEMO_H
/* PARCHE 116: demostracion reproducible del ciclo escribir -> probar -> detectar -> corregir (comando `demo_ai`).
 * Con un "modelo" guionizado y determinista (no necesita red ni LLM) el programador de SOMA genera programas que COMPILAN pero tienen errores de logica;
 * las pruebas de comportamiento los detectan ejecutandolos en la VM, el detalle (entrada, esperado, obtenido) vuelve al modelo y se corrige.
 * El guion solo corrige si el mensaje de reparacion trae realmente el resultado de las pruebas: no es una respuesta fija a ciegas.
 * Cada ejecucion imprime una huella (hash del registro): dos ejecuciones identicas dan la misma huella. `demo_ai vivo` usa el LLM real con las mismas pruebas. */
int coder_demo_run(int use_real_llm);      /* devuelve 1 si todos los escenarios se comportaron como se esperaba */
#endif
