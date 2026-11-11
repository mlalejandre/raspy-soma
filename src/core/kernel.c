#include "fb.h"
#include "arch.h"
#include <stdint.h>
#include "console.h"
#include "idt.h"
#include "pmm.h"
#include "vmm.h"
#include "mem.h"
#include "thread.h"
#include "sysinfo.h"
#include "pci.h"
#include "virtio_blk.h"
#include "fs.h"
#include "boot_gate.h"
#include "llm.h"
#include "net.h"
#include "shell.h"
#include "agent.h"
#include "ramstat.h"
#include "io.h"
#include "lang.h"
#include "usermode.h"
#include "bigfs_glue.h"
#include "audio_sys.h"
#ifdef __x86_64__
#include "xhci.h"
#endif
#ifdef __aarch64__
#include "arch/aarch64/rpi_fb.h"
#ifdef PLATFORM_rpi5
#include "arch/aarch64/rp1_gpio.h"
void xhci2_autostart(void);
#endif
#include "sdhci.h"
#include "xhci.h"
#endif

extern uint32_t multiboot_magic;
extern uint32_t multiboot_info_addr;

#define COM1 0x3F8

static void serial_init(void)
{
#ifdef __x86_64__
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
#endif
}

#ifdef __x86_64__
/* SOMA_PARCHE_010_BOOT: la prueba HTTP/LLM contra 192.168.1.200:8087 se ejecutaba aqui, de forma
 * sincrona, antes de dar el prompt: ~7 s de espera en cada arranque aunque no hubiera servidor
 * (el kernel no sabe si en el runner elegiste "sin LLM"). Es solo informativa (llm_test_passed no
 * lo lee nadie), asi que corre en un hilo de fondo. */
static void llm_selftest_thread(void *arg)
{
    (void)arg;
    net_run_llm_test();
}
#endif

/* SOMA_PARCHE_021_FPU */
static void run_autoexec_line(const char *line)
{
    static char out[512];
    kprint("[AUTOEXEC] "); kprint(line); kprint("\n");
    dispatch_command(line, out, sizeof out);
}

void kernel_main(void)
{
    serial_init();
#ifdef __aarch64__
    rpi_fb_init();                 /* PARCHE 075: consola de texto en HDMI cuanto antes (Pi 5): el arranque se ve en pantalla */
#endif
    idt_init();
    pmm_init(multiboot_magic, multiboot_info_addr);
    vmm_init();
    vmm_apply_protections();
    kheap_init();
    thread_init();
    timer_calibrate_tsc();
    timer_init(1000);
    kprint("Hardware Timer Activo a 1000 Hz\n");
    arch_interrupts_enable();
    kprint("CPU: Interrupciones configuradas\n");
    kprint("Reloj Hardware calibrado a "); kprint_dec((uint32_t)tsc_freq_mhz);
    kprint(" MHz ("); kprint_dec((uint32_t)tsc_ticks_per_ms); kprint(" ticks/ms)\n");

    virtio_blk_init();

#ifdef __aarch64__
    (void)rpi_fb_init();
#endif

    vfs_init();
    usermode_install_programs();   /* SOMA_PARCHE_016_EXEC: /bin/*.elf */
    bigfs_sys_probe();             /* SOMA_PARCHE_018_BIGFS */
    audio_sys_init();              /* SOMA_PARCHE_020_AUDIO */
    lang_init();

    net_init(); /* PARCHE 047: la red debe estar viva antes del canary (ping al gateway) */
    boot_gate_check();

#ifndef __x86_64__
    kprint("NET: Red configurada (test automatico LLM omitido en aarch64)\n");
#endif

    static char dummy_fb[64];
    show_somafetch(dummy_fb, sizeof(dummy_fb));
    ramstat_print();

    kprint("\033[1;36m----------------------------------------------------------------------\033[0m\n");
    kprint(" \033[1;37mSOMA 0.2 :: Escribe '\033[1;32msoma <mision>\033[1;37m' para el Agente o '\033[1;33mhelp\033[1;37m'.\033[0m\n");
    kprint("\033[1;36m----------------------------------------------------------------------\033[0m\n\n");

    agent_state_load();
    agent_refresh_knowledge(); /* MAYORDOMO_V2_KNOWLEDGE */
    console_history_load_from_vfs();

    static char init_dummy[128];
    char chk_init[16];
    if (vfs_read("/etc/init.sh", chk_init, sizeof(chk_init)) > 0) {
        kprint("\033[1;33m[INIT]: Ejecutando /etc/init.sh...\033[0m\n");
        dispatch_command("source /etc/init.sh", init_dummy, sizeof(init_dummy));
        kprint("\n");
    }

    /* SOMA_PARCHE_021_FPU: /autoexec.cmd del almacen grande (si existe), antes de la GUI */
    if (bigfs_sys_autoexec(run_autoexec_line)) kprint("[AUTOEXEC] terminado\n");

#ifdef __aarch64__
    sd_autostart();                                     /* PARCHE 061: tarjeta SD (Pi 5) */
#endif
#if defined(__x86_64__) || defined(__aarch64__)
    xhci_autostart();                                   /* PARCHE 048: teclado/raton USB (si hay xHCI) antes de abrir el escritorio */
#endif
#ifdef PLATFORM_rpi5
    xhci2_autostart();                                  /* PARCHE 077: segundo controlador USB del RP1 (los otros puertos de la placa) */
    if (rp1_fan_set(1) == 0) kprint("FAN: ventilador en marcha (GPIO45; 'fan off' lo para, 'fan inv' si gira al reves)\n");
#endif
    kprint("\033[1;32m[SOMA ready]\033[0m Sistema interactivo listo en consola serie.\n\n");
    kprint("\033[1;36m[DESKTOP]: Iniciando Entorno Grafico SOMA por defecto...\033[0m\n");
#ifdef __x86_64__
    thread_create("llmtest", llm_selftest_thread, 0);   /* arranca cuando kernel_main ceda la CPU */
#endif
#ifdef PLATFORM_rpi5
    kprint("Escritorio grafico: escribe 'vsoma' (en la Pi 5 el arranque queda en la consola serie hasta que haya teclado USB real)\n");
#else
    cmd_vsoma("", 0, 0);
#endif

    kprint("\033[1;32m[SOMA ready]\033[0m Modo texto activado.\n\n");
    shell_run();
}
