#ifndef SOMA_WIFI_H
#define SOMA_WIFI_H
/* PARCHE 079: Wi-Fi de la Pi 5, fase 1 (bus y arranque del chip). Controlador SDHCI de SDIO2 por sondeo, alimentacion WL_ON, enumeracion
 * SDIO, acceso al bus interno del chip CYW43455, lista de nucleos, carga del firmware y la NVRAM desde la tarjeta SD (carpeta /wifi) y
 * lectura de la consola interna del firmware. Aun no hay escaneo ni conexion (fase 2: protocolo de control del firmware). */
#include <stdint.h>

/* Placa segun el firmware (mailbox). 0 = leida. */
int  board_detect(void);
void board_print(void);
int  board_has_wifi(void);

/* Ordenes del shell: "wifi", "wifi scan", "wifi conectar <red> <clave>", "wifi desconectar", "wifi prueba", "wifi sondear", "wifi nucleos", "wifi firmware", "wifi log", "wifi mux [n]",
 * "wifi rd <dir>", "wifi wr <dir> <valor>". */
void wifi_command(const char *args);
/* Estado: 0 apagado, 1 chip respondiendo, 2 nucleos leidos, 3 firmware en marcha. */
int  wifi_state(void);
/* PARCHE 081: atiende las tramas pendientes del chip (lo llama la interfaz wlan0 al sondear). */
void wifi_service(void);
#endif
