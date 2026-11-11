ENTRY(_start)
SECTIONS
{
    . = DEFINED(SOMA_BASE) ? SOMA_BASE : 0x40080000;   /* PARCHE 058: --defsym=SOMA_BASE=... (Raspberry Pi: 0x80000) */
    .text : {
        __text_start = .;
        *(.text.boot)
        *(.text*)
        __text_end = .;
    }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(16);
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    . = ALIGN(4096);
    .virtio_queue : { *(.virtio_queue) }
    /* PARCHE 058: pila del arranque dentro de la imagen (ya no en una direccion fija de QEMU virt) */
    .boot_stack (NOLOAD) : { . = ALIGN(4096); . += 0x40000; __stack_top = .; }
}
