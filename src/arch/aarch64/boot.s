.section .text.boot
.global _start
_start:
    /* PARCHE 033: el cargador entrega la direccion del arbol de dispositivos (DTB) en x0; se guarda en x19 */
    mov x19, x0
    mrs x20, CurrentEL              /* PARCHE 082: nivel de entrada (decide SMC o HVC para PSCI) */
    /* Leer ID del core y detener núcleos secundarios */
    mrs x0, mpidr_el1
    and x0, x0, #0xFF
    cbz x0, master
hang:
    wfe
    b hang

master:
    /* PARCHE 058: el firmware de la Raspberry Pi (y QEMU con virtualization=on) entran en EL2; se baja a EL1 (x19 conserva el DTB) */
    mrs x0, CurrentEL
    lsr x0, x0, #2
    cmp x0, #2
    b.ne el1_entry
    mrs x0, cnthctl_el2
    orr x0, x0, #3                  /* EL1 puede usar el contador y el temporizador fisicos */
    msr cnthctl_el2, x0
    msr cntvoff_el2, xzr
    mov x0, #(1 << 31)              /* HCR_EL2.RW: EL1 en AArch64 */
    msr hcr_el2, x0
    mov x0, #0x33ff                 /* CPTR_EL2: sin trampas de FP/SIMD */
    msr cptr_el2, x0
    msr hstr_el2, xzr
    mov x0, #0x0800
    movk x0, #0x30d0, lsl #16       /* SCTLR_EL1: valor de reinicio (bits RES1), MMU y caches apagadas */
    msr sctlr_el1, x0
    mov x0, #0x3c5                  /* SPSR_EL2: EL1h con DAIF enmascarados */
    msr spsr_el2, x0
    ldr x0, =el1_entry
    msr elr_el2, x0
    eret
el1_entry:
    /* 1. Inicializar Stack Pointer (pila dentro de la imagen, ver linker_aarch64.ld) */
    ldr x1, =__stack_top
    mov sp, x1

    /* 2. Configurar la tabla de vectores de excepciones VBAR_EL1 */
    ldr x0, =vectors
    msr vbar_el1, x0
    isb

    /* 3. Limpiar la sección .bss completa a cero (16 bytes por paso) */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    sub x2, x1, x0
    cbz x2, bss_done
bss_loop:
    stp xzr, xzr, [x0], #16
    cmp x0, x1
    b.lo bss_loop
bss_done:
    ldr x1, =soma_dtb_addr
    str x19, [x1]
    lsr x20, x20, #2
    ldr x1, =soma_boot_el
    str x20, [x1]

    bl mmu_early_init           /* PARCHE 060: MMU y cachés */
    bl kernel_main
    b hang

/* ====================================================================
 * PARCHE 082: entrada de los nucleos secundarios (PSCI CPU_ON). x0 = indice del nucleo (1..3).
 * Baja a EL1 si hace falta, activa la MMU con los valores que dejo el nucleo 0 en smp_mmu_cfg (escritos hasta la RAM: aqui la MMU y
 * las caches estan apagadas, asi que no se toca la pila hasta tenerlas), pone su pila y salta a smp_secondary_main(indice).
 * ==================================================================== */
.global smp_secondary_entry
smp_secondary_entry:
    mov x19, x0
    mrs x0, CurrentEL
    lsr x0, x0, #2
    cmp x0, #2
    b.ne 2f
    mrs x0, cnthctl_el2
    orr x0, x0, #3
    msr cnthctl_el2, x0
    msr cntvoff_el2, xzr
    mov x0, #(1 << 31)
    msr hcr_el2, x0
    mov x0, #0x33ff
    msr cptr_el2, x0
    msr hstr_el2, xzr
    mov x0, #0x0800
    movk x0, #0x30d0, lsl #16
    msr sctlr_el1, x0
    mov x0, #0x3c5
    msr spsr_el2, x0
    adr x0, 2f
    msr elr_el2, x0
    eret
2:
    msr daifset, #0xf
    ldr x2, =smp_mmu_cfg
    ldr x3, [x2]
    msr mair_el1, x3
    ldr x3, [x2, #8]
    msr tcr_el1, x3
    ldr x3, [x2, #16]
    msr ttbr0_el1, x3
    isb
    tlbi vmalle1
    dsb nsh
    ic iallu
    dsb nsh
    isb
    ldr x3, [x2, #24]
    msr sctlr_el1, x3
    isb
    ldr x0, =vectors
    msr vbar_el1, x0
    isb
    ldr x1, =smp_stacks
    add x2, x19, #1
    lsl x2, x2, #16                 /* 64 KiB por nucleo */
    add x1, x1, x2
    mov sp, x1
    mov x0, x19
    bl smp_secondary_main
3:
    wfe
    b 3b

/* ====================================================================
 * TABLA VECTORIAL DE EXCEPCIONES AARCH64 (VBAR_EL1)
 * ==================================================================== */
.align 11
.global vectors
vectors:
    /* Current EL with SP0 */
    .align 7
    b exc_entry_curr_sp0_sync
    .align 7
    b exc_entry_curr_sp0_irq
    .align 7
    b exc_entry_curr_sp0_fiq
    .align 7
    b exc_entry_curr_sp0_serror

    /* Current EL with SPx */
    .align 7
    b exc_entry_curr_spx_sync
    .align 7
    b exc_entry_curr_spx_irq
    .align 7
    b exc_entry_curr_spx_fiq
    .align 7
    b exc_entry_curr_spx_serror

    /* Lower EL using AArch64 */
    .align 7
    b exc_entry_lower_a64_sync
    .align 7
    b exc_entry_lower_a64_irq
    .align 7
    b exc_entry_lower_a64_fiq
    .align 7
    b exc_entry_lower_a64_serror

    /* Lower EL using AArch32 */
    .align 7
    b exc_entry_lower_a32_sync
    .align 7
    b exc_entry_lower_a32_irq
    .align 7
    b exc_entry_lower_a32_fiq
    .align 7
    b exc_entry_lower_a32_serror

.macro EXC_STUB name, type
\name:
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]

    mov x0, #\type
    mov x1, sp
    bl arm64_exception_handler

    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #256
    eret
.endm

EXC_STUB exc_entry_curr_sp0_sync, 0
EXC_STUB exc_entry_curr_sp0_irq, 1
EXC_STUB exc_entry_curr_sp0_fiq, 2
EXC_STUB exc_entry_curr_sp0_serror, 3

EXC_STUB exc_entry_curr_spx_sync, 4
EXC_STUB exc_entry_curr_spx_irq, 5
EXC_STUB exc_entry_curr_spx_fiq, 6
EXC_STUB exc_entry_curr_spx_serror, 7

EXC_STUB exc_entry_lower_a64_sync, 8
EXC_STUB exc_entry_lower_a64_irq, 9
EXC_STUB exc_entry_lower_a64_fiq, 10
EXC_STUB exc_entry_lower_a64_serror, 11

EXC_STUB exc_entry_lower_a32_sync, 12
EXC_STUB exc_entry_lower_a32_irq, 13
EXC_STUB exc_entry_lower_a32_fiq, 14
EXC_STUB exc_entry_lower_a32_serror, 15

.section .data
.align 8
.global multiboot_magic
.global multiboot_info_addr
multiboot_magic:
    .long 0
multiboot_info_addr:
    .long 0

.section .data
.align 3
.global soma_dtb_addr
soma_dtb_addr:
    .quad 0
