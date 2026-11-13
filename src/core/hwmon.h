#ifndef SOMA_HWMON_H
#define SOMA_HWMON_H
/* PARCHE 089: datos de hardware para el Monitor del sistema (cada arquitectura los rellena a su manera; lo que no se sabe va a 0 o ""). */
#include <stdint.h>
struct hw_cpu {
    char arch[16], vendor[24], brand[64], hyper[24];
    int  family, model, stepping, cores, threads;
    uint32_t mhz, max_mhz, l1d_kb, l1i_kb, l2_kb, l3_kb;
    char feats[200];
    int  temp_mc;              /* milesimas de grado; -1 = no disponible */
    int  volt_mv;              /* -1 = no disponible */
};
struct hw_gpu { char name[64], vendor[32]; uint32_t core_mhz, v3d_mhz, mem_mb; int temp_mc; };
struct hw_board { char model[64], firmware[40], serial[24]; uint32_t ram_mb, gpu_mem_mb, revision; };
struct hw_dev { char bus[8], name[64], extra[40]; uint16_t vendor, device; uint32_t cls; };
int hwmon_cpu(struct hw_cpu *o);
int hwmon_gpu(struct hw_gpu *o);
int hwmon_board(struct hw_board *o);
int hwmon_devices(struct hw_dev *o, int max);
int hwmon_live(int *temp_mc, uint32_t *mhz);   /* lectura rapida (cada segundo) de temperatura y frecuencia; 0 = hay datos */
#endif
