// Flat guest memory: the D1 decision (see handover doc v2.3, section 3.4).
// The translated corpus computes every address as an explicit uint32_t
// expression and never forms a native pointer, so the whole 32-bit guest
// space is one 4 GiB arena and guest->host translation is base + addr.
#ifndef ISAAC_GUEST_MEMORY_H
#define ISAAC_GUEST_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define ISAAC_GUEST_MEM_SIZE 0x100000000ULL /* 4 GiB */

/* Reserve the guest arena (MAP_NORESERVE: pages commit lazily on touch).
 * Returns 0 on success. Must be called once before any GL backend use. */
int guest_mem_init(void);

void guest_mem_shutdown(void);

/* Host pointer for a guest address. No bounds check by design: the corpus
 * is trusted to stay in 32 bits (the compiler masks every address). */
static inline uint8_t *guest_mem_ptr(uint32_t addr) {
    extern uint8_t *isaac_arena_base;
    return isaac_arena_base + addr;
}

/* Bump-allocate a NUL-terminated copy of s in the arena (used to hand guest
 * addresses back for glGetString etc.). Returns the guest address, 0 on
 * exhaustion. Not thread-safe: call from the render thread only. */
uint32_t guest_mem_push_string(const char *s, size_t len);

/* Current bump offset (also the first free guest address). */
uint32_t guest_mem_top(void);

#endif /* ISAAC_GUEST_MEMORY_H */
