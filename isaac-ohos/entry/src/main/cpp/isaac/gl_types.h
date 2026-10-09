// Minimal guest-GL type surface for the OHOS backend. Mirrors the typedef
// block of repentogxm's gl_bridge.h so the frozen generated header
// (gl_surface_generated.h) compiles without pulling in the whole guest
// runtime (guest.h/CPU). The authoritative contract stays upstream; this
// file only re-declares the data types the backend touches.
#ifndef ISAAC_GL_TYPES_H
#define ISAAC_GL_TYPES_H

#include <stdint.h>

typedef uint32_t guest_gl_addr;
typedef uint32_t guest_gl_enum;
typedef uint32_t guest_gl_bitfield;
typedef uint32_t guest_gl_uint;
typedef int32_t  guest_gl_int;
typedef int32_t  guest_gl_sizei;
typedef uint8_t  guest_gl_boolean;
typedef float    guest_gl_float;
typedef double   guest_gl_double;

#define REPENTOGXM_GL_BRIDGE_TYPES_READY 1
#include "gl_surface_generated.h"
#undef REPENTOGXM_GL_BRIDGE_TYPES_READY

#endif /* ISAAC_GL_TYPES_H */
