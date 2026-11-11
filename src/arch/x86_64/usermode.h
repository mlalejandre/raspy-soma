#ifndef MYOS_USERMODE_H
#define MYOS_USERMODE_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_014_RING3: ejecucion de codigo en anillo 3 (CPL=3).
 *
 * Mapa de usuario (PML4[1] = 512 GiB: cada proceso tiene su PROPIA tabla de paginas y la del kernel no tiene nada ahi):
 *   USER_BASE .. USER_BASE+USER_WINDOW
 * Llamadas al sistema (instruccion SYSCALL): rax = numero, rdi/rsi/rdx = argumentos, rax = resultado.
 * Se conservan rbx, rbp, r12-r15 y rsp; el resto (rcx, r11, rdi, rsi, rdx, r8-r10) se destruye.
 */
#define USER_BASE    0x8000000000ULL      /* PML4[1]: region de usuario, vacia en la PML4 del kernel (parche 017) */
/* SOMA_PARCHE_019_FILES: region de usuario ampliada para programas grandes.
 *   codigo + datos + bss : USER_BASE .. +4 MiB (USER_CODE_AREA, hasta USER_CODE_MAX_PAGES paginas)
 *   pila                 : USER_STACK_VA, USER_STACK_PAGES paginas (la pila crece hacia abajo desde su cima) */
#define USER_CODE_AREA      0x400000ULL           /* 4 MiB */
#define USER_CODE_MAX_PAGES 1024
#define USER_WINDOW  0x2000000ULL                 /* 32 MiB: todo lo que un proceso puede tocar y pasar al kernel */
#define USER_CODE_VA USER_BASE
#define USER_STACK_VA (USER_BASE + 0x1000000ULL)

#define SYS_EXIT   0
#define SYS_WRITE  1   /* write(ptr, len) -> bytes escritos o -1 (EFAULT) */
#define SYS_YIELD  2
#define SYS_SLEEP  3   /* sleep(ms) */
#define SYS_GETPID 4   /* -> tid del hilo */
/* SOMA_PARCHE_019_FILES: ficheros del almacen BIGFS (solo lectura) y argumentos. Los errores son codigos negativos:
 * -14 puntero invalido, -9 descriptor invalido, -22 argumento invalido, -24 demasiados ficheros abiertos y los
 * de BIGFS (-2 no existe, -5 es un directorio, ...). */
#define SYS_OPEN    5   /* open(ruta_ptr, ruta_len, flags=0) -> fd >= 0 */
#define SYS_READ    6   /* read(fd, buf, len) -> bytes leidos (0 = fin); como mucho 64 KiB por llamada */
#define SYS_SEEK    7   /* seek(fd, desplazamiento, 0=desde el inicio | 1=desde la posicion | 2=desde el final) -> nueva posicion */
#define SYS_CLOSE   8   /* close(fd) */
#define SYS_FSIZE   9   /* fsize(fd) -> tamano en bytes */
#define SYS_GETARGS 10  /* getargs(buf, len) -> longitud de la cadena de argumentos (se copia con NUL) */
/* SOMA_PARCHE_020_AUDIO: salida de audio PCM 16 bits estereo (un solo flujo en todo el sistema).
 *   audio_open(frecuencia) -> 0 | -16 ocupado | -19 sin dispositivo | -22 frecuencia no soportada
 *   audio_write(buf, len)  -> bytes aceptados (bloquea hasta aceptarlos todos o 3 s sin avance); len multiplo de 4, <= 64 KiB
 *   audio_close(esperar)   -> 0 (esperar=1: deja que termine de sonar lo escrito) */
#define SYS_AUDIO_OPEN  11
#define SYS_AUDIO_WRITE 12
#define SYS_AUDIO_CLOSE 13
/* SOMA_PARCHE_021_FPU: pantalla en exclusiva (la GUI no dibuja mientras dura). Pixeles XRGB de 32 bits (0x00RRGGBB).
 *   fb_open(ancho, alto)           -> 0 | -16 ocupada | -19 sin pantalla grafica | -22 tamano no valido (ancho multiplo de 8, <= 1920x1080)
 *   fb_info(buf)                   -> 0; escribe 3 uint32: ancho, alto, bytes por fila
 *   fb_blit(buf, w<<16|h, x<<16|y) -> 0; copia un rectangulo w x h (fila a fila, w*4 bytes por fila) y lo presenta
 *   fb_close()                     -> 0; la GUI (o la consola de texto) recupera la pantalla
 * Se libera sola si el proceso termina. */
#define SYS_FB_OPEN  14
#define SYS_FB_INFO  15
#define SYS_FB_BLIT  16
#define SYS_FB_CLOSE 17
/* SOMA_PARCHE_022_VIDEO */
#define SYS_AUDIO_STATUS 18   /* audio_status(u64 out[3]) -> 0; out = {espacio libre FIFO en bytes, reloj de audio en us, huecos} */
#define SYS_UPTIME_MS    19   /* -> milisegundos desde el arranque */
#define USER_MAX_FDS 8
#define USER_ARGS_MAX 128

/* Codigos de salida negativos de usermode_run_code() */
#define USER_EXIT_FAULT_BASE (-1000)   /* -1000 - numero de vector de la excepcion de CPU */
#define USER_EXIT_TIMEOUT    (-2000)   /* superó el limite de tiempo */

#define USER_MAX_PROCS 8
#define USER_STACK_PAGES 16     /* 64 KiB de pila de usuario */

/* SOMA_PARCHE_015_PROC / 017_AS: cada proceso es un hilo del planificador con su propia pila de kernel y su propio
 * espacio de direcciones (CR3 propio). Todos ven las MISMAS direcciones virtuales (USER_CODE_VA / USER_STACK_VA)
 * y un proceso nunca ve la memoria de otro. Con SMEP/SMAP (si la CPU los tiene) el kernel tampoco puede
 * ejecutar ni leer por error memoria de usuario.
 * El reloj puede quitar la CPU a un proceso SOLO mientras esta en ring 3 (el kernel sigue siendo cooperativo). */
int     usermode_spawn(const char *name, const void *code, size_t len, uint32_t max_ms);   /* >=0: indice; <0 error */
int64_t usermode_wait(int idx);

/* SOMA_PARCHE_016_EXEC: carga un ejecutable ELF64 estatico (validado por elf_parse) como proceso nuevo.
 * Devuelve el indice o <0; *err recibe el codigo ELF_E_* si el archivo no es valido. */
int usermode_exec(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, int *err);
int usermode_format_table(char *out, uint32_t max, int reap);   /* lista de procesos; reap=1 libera los terminados */
void usermode_install_programs(void);                            /* copia los ELF del kernel a /bin/ */                                                             /* espera y libera */

/* Copia `code` en una pagina de usuario, lo ejecuta en ring 3 y devuelve el codigo de salida (spawn+wait). */
int64_t usermode_run_code(const void *code, size_t len, uint32_t max_ms);

/* Prueba integral (syscalls+CPL, #PF, #GP, plazo; despues concurrencia: preemption, aislamiento entre procesos, fallo de uno sin afectar a otro).
 * Devuelve 0 si todo se comporta como se espera. */
int usermode_selftest(void);
int usermode_cpu_features(void);     /* bit0 = SMEP activo, bit1 = SMAP activo */

/* Ganchos que llama el manejador de interrupciones (idt.c) */
int  usermode_active(void);
void usermode_on_fault(uint64_t vector, uint64_t rip, uint64_t cr2);   /* no vuelve si estaba en ring 3 */
void usermode_on_tick(uint64_t now_ms, uint64_t cs);                   /* no vuelve si vencio el plazo */

#endif
