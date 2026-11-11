/* io.h de sustitucion SOLO para pruebas en host: misma API que src/arch/x86_64/io.h
 * pero sin ensamblador, para compilar igual en x86_64 y en Apple Silicon. */
#ifndef MYOS_IO_H
#define MYOS_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value) { (void)port; (void)value; }
static inline void io_wait(void) { }
static inline uint8_t inb(uint16_t port) { (void)port; return 0; }
static inline void outw(uint16_t port, uint16_t value) { (void)port; (void)value; }
static inline uint16_t inw(uint16_t port) { (void)port; return 0; }
static inline void outl(uint16_t port, uint32_t value) { (void)port; (void)value; }
static inline uint32_t inl(uint16_t port) { (void)port; return 0; }
static inline uint64_t rdtsc(void) { return 0; }
static inline void mem_barrier(void) { }
static inline void cpu_pause(void) { }

#endif
