#ifndef MYOS_AUDIO_SYS_H
#define MYOS_AUDIO_SYS_H

#include <stdint.h>

/* SOMA_PARCHE_020_AUDIO: pegamento del audio con el kernel (hilo audiod, comandos, API para procesos). */
void audio_sys_init(void);                              /* arranque: hda_init + hilo audiod (no falla si no hay dispositivo) */
int  audio_sys_available(void);
int  audio_sys_open(uint32_t rate, uint32_t owner_tid); /* 0 = OK; -16 ocupado; -19 sin dispositivo; -22 frecuencia no soportada */
int  audio_sys_write(const void *pcm, uint32_t n);      /* bloquea hasta aceptarlo todo (o 3 s sin avance); devuelve los bytes aceptados */
int  audio_sys_status(uint64_t out[3]);                 /* out = {espacio libre en la FIFO (bytes), reloj de audio (us), huecos}; 0 = OK */
uint32_t audio_sys_flush(void);                        /* descarta la FIFO y devuelve los bytes descartados (0 si cerrado) */
uint32_t audio_sys_fifo_bytes(void);                   /* capacidad de la FIFO */
void audio_sys_close(int drain);                        /* drain = esperar a que suene lo escrito */
void audio_sys_owner_died(uint32_t tid);                /* un proceso termino: si tenia el audio abierto, se cierra */
int  audio_sys_command(const char *args, char *out, uint32_t max);   /* shell: audio info|tone|test|play|stop|vol|stat */

#endif
