/* SOMA_PARCHE_016_EXEC: enlazado de programas de usuario (ET_EXEC, direccion fija en la ventana de usuario).
 * Dos segmentos separados por pagina: codigo+rodata (R+X) y datos+bss (R+W). Nunca W+X. */
ENTRY(_start)
PHDRS
{
    text PT_LOAD FLAGS(5);   /* R + X */
    data PT_LOAD FLAGS(6);   /* R + W */
}
SECTIONS
{
    . = 0x8000000000;      /* region de usuario: PML4[1] (ver usermode.h) */
    .text   : { *(.text._start) *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text
    . = ALIGN(4096);
    .data   : { *(.data .data.*) } :data
    .bss    : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu*) }
}
