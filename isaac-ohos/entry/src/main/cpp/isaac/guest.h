/* Guest CPU model for the recompiled Isaac — HarmonyOS/ARM64 adaptation.
 *
 * Based on repentogxm's guest.h. Key adaptation: memory access uses a flat
 * 4 GiB arena (base + addr) instead of guest-addr == host-addr mapping.
 * All generated code interfaces (CPU struct, GR/GPUSH/GESP macros, ld/st,
 * flags, SSE lowering, dispatch) are preserved exactly.
 *
 * Configuration for initial port (simplified):
 *   GUEST_GPR_LOCAL=0         — registers directly in CPU struct
 *   GUEST_STACK_REQUIRED=0    — no stack bounds checking
 *   GUEST_SSE_LOWER=0         — lane loops (not NEON)
 *   GUEST_GENERATED_STACK_GUARD=0 — raw stack ops in generated code
 *   GUEST_COVERAGE_HOOKS=0    — no coverage tracking
 *   GUEST_IMPORT_DIRECT=0     — standard guest_call for imports
 */
#ifndef ISAAC_GUEST_H
#define ISAAC_GUEST_H

#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ types */

typedef union {
    float    f[4];
    double   d[2];
    int8_t   i8[16];
    uint8_t  u8[16];
    int16_t  i16[8];
    uint16_t u16[8];
    int32_t  i32[4];
    uint32_t u32[4];
    int64_t  i64[2];
    uint64_t u64[2];
} xmm_t;

typedef struct guest_jump_site {
    jmp_buf native_env;
    struct guest_jump_site *previous;
    uint32_t guest_env;
    uint32_t guest_esp;
    uint32_t guest_eip;
    uint32_t guest_registration;
} guest_jump_site;

enum {
    FLAG_NONE = 0,
    FLAG_ADD, FLAG_SUB, FLAG_LOGIC, FLAG_INC, FLAG_DEC,
    FLAG_SHL, FLAG_SHR, FLAG_SAR, FLAG_NEG, FLAG_IMUL, FLAG_MUL,
    FLAG_BT,
    FLAG_EXPLICIT,
    FLAG_PARTIAL
};

enum {
    GUEST_STACK_FAULT_NONE = 0,
    GUEST_STACK_FAULT_UNBOUND,
    GUEST_STACK_FAULT_OWNER,
    GUEST_STACK_FAULT_SET,
    GUEST_STACK_FAULT_ADJUST,
    GUEST_STACK_FAULT_ACCESS,
    GUEST_STACK_FAULT_PUSH,
    GUEST_STACK_FAULT_POP
};

typedef struct guest_flags {
    uint32_t f_a, f_b, f_r;
    uint8_t  f_op;
    uint8_t  f_sz;
    uint8_t  f_cf;
    uint8_t  f_of;
    uint8_t  f_zf, f_sf, f_pf;
} guest_flags;

typedef struct CPU {
    union {
        uint32_t r[8];
        struct { uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi; };
    };

    xmm_t x[8];

    double  st[8];
    int     st_top;

    uint16_t fsw;

    union {
        guest_flags fl;
        struct {
            uint32_t f_a, f_b, f_r;
            uint8_t  f_op;
            uint8_t  f_sz;
            uint8_t  f_cf;
            uint8_t  f_of;
            uint8_t  f_zf, f_sf, f_pf;
        };
    };
    uint8_t  df;

    const char *fault;
    uint32_t    fault_addr;

    const char *exit_api;
    int32_t     exit_code;
    int32_t     stop_kind;

    void       *run_scope;

    guest_jump_site *jump_sites;
    uint32_t         jump_value;

    struct CPU *stack_owner;
    uint32_t    stack_floor;
    uint32_t    stack_ceiling;
    uint32_t    stack_low_water;
    uint32_t    stack_fault_address;
    uint32_t    stack_fault_size;
    uint32_t    stack_fault_pc;
    uintptr_t   stack_fault_native_site;
    uint32_t    stack_fault_kind;

    uint32_t         last_error;
    uint32_t         vita_sync_thread_id;
} CPU;

typedef void (*guest_fn)(CPU *__restrict);

void guest_fault(CPU *__restrict c, uint32_t addr, const char *what);

/* ------------------------------------------------------------- memory ---- */
/* ADAPTATION: flat arena model. Guest addresses are 32-bit offsets into a
 * 4 GiB arena. The arena base is set up by guest_mem_init(). */

extern uint8_t *isaac_arena_base;

static inline uint8_t  ld8 (uint32_t a) { return *(uint8_t  *)(isaac_arena_base + a); }
static inline uint16_t ld16(uint32_t a) { return *(uint16_t *)(isaac_arena_base + a); }
static inline uint32_t ld32(uint32_t a) { return *(uint32_t *)(isaac_arena_base + a); }
static inline uint64_t ld64(uint32_t a) { uint64_t v; memcpy(&v, isaac_arena_base + a, 8); return v; }
static inline xmm_t    ldx (uint32_t a) { xmm_t v; memcpy(&v, isaac_arena_base + a, 16); return v; }

static inline void st8 (uint32_t a, uint8_t  v) { *(uint8_t  *)(isaac_arena_base + a) = v; }
static inline void st16(uint32_t a, uint16_t v) { *(uint16_t *)(isaac_arena_base + a) = v; }
static inline void st32(uint32_t a, uint32_t v) { *(uint32_t *)(isaac_arena_base + a) = v; }
static inline void st64(uint32_t a, uint64_t v) { memcpy(isaac_arena_base + a, &v, 8); }
static inline void stx (uint32_t a, xmm_t    v) { memcpy(isaac_arena_base + a, &v, 16); }

static inline double   ldd (uint32_t a) { double v; memcpy(&v, isaac_arena_base + a, 8); return v; }
static inline void std_(uint32_t a, double   v) { memcpy(isaac_arena_base + a, &v, 8); }

static inline float    ldf (uint32_t a) { float  v; memcpy(&v, isaac_arena_base + a, 4); return v; }
static inline void stf (uint32_t a, float    v) { memcpy(isaac_arena_base + a, &v, 4); }

/* Atomic 64-bit compare/exchange used by LOCK CMPXCHG8B. */
uint64_t guest_atomic_cmpxchg64(uint32_t addr, uint64_t expected, uint64_t desired);

/* Store double as x87 80-bit memory format. */
static inline void st80d(uint32_t a, double v)
{
    uint64_t raw, fraction, significand;
    uint32_t exponent;
    uint16_t sign_exponent;
    uint8_t out[10];
    unsigned i;

    memcpy(&raw, &v, sizeof raw);
    fraction = raw & UINT64_C(0x000fffffffffffff);
    exponent = (uint32_t)((raw >> 52) & 0x7ffU);
    sign_exponent = (uint16_t)((raw >> 48) & 0x8000U);

    if (exponent == 0U) {
        if (fraction == 0U) {
            significand = 0U;
        } else {
            int unbiased = -1022;
            while ((fraction & UINT64_C(0x0010000000000000)) == 0U) {
                fraction <<= 1;
                --unbiased;
            }
            significand = fraction << 11;
            sign_exponent |= (uint16_t)(unbiased + 16383);
        }
    } else if (exponent == 0x7ffU) {
        significand = UINT64_C(0x8000000000000000) | (fraction << 11);
        sign_exponent |= 0x7fffU;
    } else {
        significand = (UINT64_C(0x0010000000000000) | fraction) << 11;
        sign_exponent |= (uint16_t)((int)exponent - 1023 + 16383);
    }

    for (i = 0; i < 8; ++i)
        out[i] = (uint8_t)(significand >> (i * 8));
    out[8] = (uint8_t)sign_exponent;
    out[9] = (uint8_t)(sign_exponent >> 8);
    memcpy(isaac_arena_base + a, out, sizeof out);
}

/* ------------------------------------------------------- flag derivation */

static inline uint32_t f_mask(uint8_t sz)
{
    return sz == 1 ? 0xFFu : sz == 2 ? 0xFFFFu : 0xFFFFFFFFu;
}
static inline uint32_t f_sign(uint8_t sz)
{
    return sz == 1 ? 0x80u : sz == 2 ? 0x8000u : 0x80000000u;
}

static inline int fl_zf(const guest_flags *c) {
    return c->f_op == FLAG_PARTIAL ? c->f_zf != 0
                                  : (c->f_r & f_mask(c->f_sz)) == 0;
}
static inline int fl_sf(const guest_flags *c) {
    return c->f_op == FLAG_PARTIAL ? c->f_sf != 0
                                  : (c->f_r & f_sign(c->f_sz)) != 0;
}

static inline int fl_pf(const guest_flags *c)
{
    uint8_t v = (uint8_t)c->f_r;
    if (c->f_op == FLAG_PARTIAL) return c->f_pf != 0;
    v ^= (uint8_t)(v >> 4); v ^= (uint8_t)(v >> 2); v ^= (uint8_t)(v >> 1);
    return (v & 1) == 0;
}

static inline int fl_cf(const guest_flags *c)
{
    uint32_t m = f_mask(c->f_sz);
    switch (c->f_op) {
    case FLAG_SUB:  return (c->f_a & m) < (c->f_b & m);
    case FLAG_ADD:  return (c->f_r & m) < (c->f_a & m);
    case FLAG_NEG:  return (c->f_a & m) != 0;
    case FLAG_SHL: {
        uint32_t n = c->f_b & 31U, bits = (uint32_t)c->f_sz * 8U;
        return n && n <= bits ? ((c->f_a & m) >> (bits - n)) & 1U : 0;
    }
    case FLAG_SHR:
    case FLAG_SAR: {
        uint32_t n = c->f_b & 31U, bits = (uint32_t)c->f_sz * 8U;
        return n && n <= bits ? ((c->f_a & m) >> (n - 1U)) & 1U : 0;
    }
    case FLAG_LOGIC: return 0;
    case FLAG_INC:
    case FLAG_DEC:  return c->f_cf != 0;
    default:        return c->f_cf != 0;
    }
}

static inline int fl_of(const guest_flags *c)
{
    uint32_t s = f_sign(c->f_sz);
    switch (c->f_op) {
    case FLAG_SUB:  return (((c->f_a ^ c->f_b) & (c->f_a ^ c->f_r)) & s) != 0;
    case FLAG_ADD:  return (((c->f_a ^ c->f_r) & (c->f_b ^ c->f_r)) & s) != 0;
    case FLAG_INC:  return (c->f_r & f_mask(c->f_sz)) == s;
    case FLAG_DEC:  return (c->f_a & f_mask(c->f_sz)) == s;
    case FLAG_LOGIC: return 0;
    case FLAG_SHL:   return c->f_b == 1U &&
                             (((c->f_r & s) != 0) != (fl_cf(c) != 0));
    case FLAG_SHR:   return c->f_b == 1U && (c->f_a & s) != 0;
    case FLAG_SAR:   return 0;
    default:        return c->f_of != 0;
    }
}

static inline int cc_b (const guest_flags *c) { return  fl_cf(c); }
static inline int cc_ae(const guest_flags *c) { return !fl_cf(c); }
static inline int cc_e (const guest_flags *c) { return  fl_zf(c); }
static inline int cc_ne(const guest_flags *c) { return !fl_zf(c); }
static inline int cc_be(const guest_flags *c) { return  fl_cf(c) ||  fl_zf(c); }
static inline int cc_a (const guest_flags *c) { return !fl_cf(c) && !fl_zf(c); }
static inline int cc_s (const guest_flags *c) { return  fl_sf(c); }
static inline int cc_ns(const guest_flags *c) { return !fl_sf(c); }
static inline int cc_p (const guest_flags *c) { return  fl_pf(c); }
static inline int cc_np(const guest_flags *c) { return !fl_pf(c); }
static inline int cc_l (const guest_flags *c) { return  fl_sf(c) != fl_of(c); }
static inline int cc_ge(const guest_flags *c) { return  fl_sf(c) == fl_of(c); }
static inline int cc_le(const guest_flags *c) { return  fl_zf(c) || fl_sf(c) != fl_of(c); }
static inline int cc_g (const guest_flags *c) { return !fl_zf(c) && fl_sf(c) == fl_of(c); }
static inline int cc_o (const guest_flags *c) { return  fl_of(c); }
static inline int cc_no(const guest_flags *c) { return !fl_of(c); }

#define SET_FLAGS(c, op, a, b, r, sz) \
    do { (c)->f_op = (uint8_t)(op); (c)->f_a = (uint32_t)(a); \
         (c)->f_b = (uint32_t)(b); (c)->f_r = (uint32_t)(r); \
         (c)->f_sz = (uint8_t)(sz); } while (0)

/* GUEST_FLAGS_LOCAL=0: flags directly in CPU struct */
#define GUEST_FLAGS_DECL ((void)0)
#define GUEST_FL (&c->fl)
#define GUEST_FLAGS_LOAD(c) ((void)0)
#define GUEST_FLAGS_FLUSH(c) ((void)0)

/* ---------------------------------------------------------------- x87 ---- */

static inline void fpush(CPU *__restrict c, double v)
{
    c->st_top = (c->st_top - 1) & 7;
    c->st[c->st_top] = v;
}
static inline double fpop(CPU *__restrict c)
{
    double v = c->st[c->st_top];
    c->st_top = (c->st_top + 1) & 7;
    return v;
}
static inline void fcmp_set(CPU *__restrict c, double a, double b)
{
    c->fsw &= (uint16_t)~((1u << 8) | (1u << 10) | (1u << 14));
    if (a != a || b != b)
        c->fsw |= (uint16_t)((1u << 8) | (1u << 10) | (1u << 14));
    else if (a > b)
        ;
    else if (a < b)
        c->fsw |= (uint16_t)(1u << 8);
    else
        c->fsw |= (uint16_t)(1u << 14);
}

static inline double *fst(CPU *__restrict c, int i)
{
    return &c->st[(c->st_top + i) & 7];
}

/* ----------------------------------------------------------- dispatch ---- */

typedef struct guest_import {
    uint32_t    slot_rva;
    const char *name;
} guest_import;

void      guest_register(const uint32_t *addrs, const guest_fn *fns, uint32_t n);
guest_fn  guest_lookup(uint32_t addr);
const uint32_t *guest_registered_addresses(uint32_t *count);

#define GUEST_IMPORT_DIRECT 0
#define GUEST_IMPORT_CALL(target, slot_rva, import_id) guest_call(c, (target))
#define GUEST_IMPORT_JMP(target, slot_rva, import_id) guest_call(c, (target))

/* Coverage hooks — disabled for initial port */
#define GUEST_COVERAGE_HOOKS 0
extern unsigned char *g_guest_coverage_functions;
extern unsigned char *g_guest_coverage_imports;
extern unsigned char *g_guest_coverage_cases;

static inline void guest_coverage_function(uint32_t id) { (void)id; }
static inline void guest_coverage_import(uint32_t id) { (void)id; }
static inline void guest_coverage_case(uint32_t id) { (void)id; }

#define GUEST_PHASE_PROFILE_NOTE_CALL() ((void)0)
#define GUEST_PHASE_PROFILE_NOTE_LOOKUP() ((void)0)
#define GUEST_PHASE_PROFILE_ADD_LOOKUP_ITERATIONS(count) ((void)0)
#define GUEST_PHASE_PROFILE_NOTE_DISPATCH_CALL() ((void)0)
#define GUEST_PHASE_PROFILE_NOTE_DISPATCH_SLOW() ((void)0)
#define GUEST_PHASE_PROFILE_NOTE_SYNC_FASTPATH() ((void)0)
#define GUEST_PHASE_PROFILE_NOTE_DISPATCH_GL() ((void)0)
#define GUEST_PHASE_PROFILE_NOTE_IMPORT(import_id) ((void)(import_id))
#define GUEST_PROFILE_NOTE_FUNCTION_ENTRY() ((void)0)

void guest_register_coverage_contract(const char *schema_id,
                                      const char *build_id,
                                      uint32_t function_count,
                                      uint32_t import_count,
                                      uint32_t case_count);
int  guest_coverage_init_from_env(void);
void guest_coverage_shutdown(void);
const char *guest_coverage_error(void);

void        guest_note_generation(const char *generation_id);
const char *guest_generation_id(void);

void        guest_register_imports(const guest_import *imports, uint32_t n);
void        guest_reregister_imports(void);
const char *guest_import_name(uint32_t slot_addr);

int guest_host_import(CPU *__restrict c, const char *name);
extern unsigned g_host_import_calls;

int guest_host_dynamic(CPU *__restrict c, uint32_t token);
extern unsigned g_host_dynamic_calls;

extern int g_guest_gl_inventory_mode;

int guest_kage_pc_handoff_ready(void);

void guest_cpuid(CPU *__restrict c);
void guest_xgetbv(CPU *__restrict c);

uint32_t guest_fs_base(CPU *__restrict c);
extern uint32_t g_guest_fs_base_cached;

void guest_call(CPU *__restrict c, uint32_t addr);

/* Callee-saved register check around direct calls (ISAAC_TRACE_ABI). */
void guest_abi_flush(CPU *__restrict c);
void guest_abi_reload(CPU *__restrict c);

void guest_setjmp_prepare(CPU *__restrict c, guest_jump_site *site);
void guest_setjmp_finish(CPU *__restrict c, guest_jump_site *site, int resumed);
void guest_setjmp_leave(CPU *__restrict c, guest_jump_site *site);

#if defined(__GNUC__)
#define GUEST_NORETURN __attribute__((noreturn))
#else
#define GUEST_NORETURN
#endif
GUEST_NORETURN void guest_longjmp(CPU *__restrict c, uint32_t guest_env, int32_t value);

enum {
    GUEST_RUN_INVALID  = -1,
    GUEST_RUN_RETURNED = 0,
    GUEST_RUN_FAULT    = 1,
    GUEST_RUN_EXIT     = 2
};

int guest_run_until_stop(CPU *__restrict c, guest_fn entry);
void guest_exit(CPU *__restrict c, int32_t code, const char *api);
void guest_int3(CPU *__restrict c, uint32_t addr);
extern uint32_t g_guest_checkpoint_rva;
int  guest_checkpoint(CPU *__restrict c, uint32_t rva);

/* ------------------------------------------------------------- image ----- */

#ifndef GUEST_IMAGE_BASE
#define GUEST_IMAGE_BASE 0x30000000u
#endif

static inline int guest_direct_translated_target(
    uint32_t target, uint32_t exact_rva)
{
    return target == exact_rva ||
           target == (uint32_t)(GUEST_IMAGE_BASE + exact_rva);
}

int  guest_image_load(const char *path);
void guest_image_free(void);
int guest_image_contains(uint32_t address, uint32_t size);
int guest_tls_process_attach(CPU *__restrict c);

void guest_cpu_init(CPU *c);

#ifndef GUEST_STACK_SIZE
#define GUEST_STACK_SIZE (4u * 1024u * 1024u)
#endif
int  guest_stack_init(CPU *c);
void guest_stack_free(CPU *c);
int  guest_stack_bind(CPU *c, uint32_t floor, uint32_t ceiling);

/* ------------------------------------------ stack (GUEST_STACK_REQUIRED=0) */
/* Raw stack operations — no bounds checking for initial port. */

static inline int guest_stack_set_generated(CPU *__restrict c, uint32_t value)
{
    c->esp = value;
    return 1;
}
static inline int guest_stack_adjust_generated(CPU *__restrict c, uint32_t amount)
{
    c->esp += amount;
    return 1;
}
static inline uint32_t guest_stack_address_generated(
    CPU *__restrict c, uint32_t address, uint32_t size)
{
    (void)c; (void)size;
    return address;
}
static inline void gpush_generated(CPU *__restrict c, uint32_t value)
{
    c->esp -= 4U;
    st32(c->esp, value);
}
static inline uint32_t gpop_generated(CPU *__restrict c)
{
    uint32_t value = ld32(c->esp);
    c->esp += 4U;
    return value;
}
static inline void gpush_at(CPU *__restrict c, uint32_t value, uint32_t pc)
{
    (void)pc;
    c->esp -= 4U;
    st32(c->esp, value);
}
static inline uint32_t gpop_at(CPU *__restrict c, uint32_t pc)
{
    (void)pc;
    uint32_t value = ld32(c->esp);
    c->esp += 4U;
    return value;
}

#define GUEST_STACK_CALLSITE_BARRIER() ((void)0)

/* ----------------------------------------------------------- SSE tokens ---- */
/* GUEST_SSE_LOWER=0: lane loops / union copies (default, portable C) */

#define GUEST_XMM_MOV(d, s) \
    do { c->x[d] = c->x[s]; } while (0)
#define GUEST_XMM_MOV0(d, s) \
    do { c->x[d] = c->x[s]; } while (0)
#define GUEST_XMM_LDX(d, a) \
    do { c->x[d] = ldx(a); } while (0)
#define GUEST_XMM_STX(a, s) \
    do { stx((a), c->x[s]); } while (0)
#define GUEST_XMM_LANEOP_R(fld, n, op, d, s) \
    do { xmm_t _s = c->x[s]; int _i; \
         for (_i = 0; _i < (n); _i++) c->x[d].fld[_i] op##= _s.fld[_i]; } while (0)
#define GUEST_XMM_LANEOP_M(fld, n, op, d, a) \
    do { xmm_t _s = ldx(a); int _i; \
         for (_i = 0; _i < (n); _i++) c->x[d].fld[_i] op##= _s.fld[_i]; } while (0)
#define GUEST_XMM_LANEOP0_R(fld, n, op, d, s) GUEST_XMM_LANEOP_R(fld, n, op, d, s)
#define GUEST_XMM_LANEOP0_M(fld, n, op, d, a) GUEST_XMM_LANEOP_M(fld, n, op, d, a)
#define GUEST_XMM_PANDN_R(d, s) \
    do { xmm_t _d = c->x[d], _s = c->x[s]; int _i; \
         for (_i = 0; _i < 4; _i++) c->x[d].u32[_i] = (~_d.u32[_i]) & _s.u32[_i]; } while (0)
#define GUEST_XMM_PANDN_M(d, a) \
    do { xmm_t _d = c->x[d], _s = ldx(a); int _i; \
         for (_i = 0; _i < 4; _i++) c->x[d].u32[_i] = (~_d.u32[_i]) & _s.u32[_i]; } while (0)
#define GUEST_XMM_CVTDQ2PS_R(d, s) \
    do { xmm_t _s = c->x[s]; int _i; \
         for (_i = 0; _i < 4; _i++) c->x[d].f[_i] = (float)_s.i32[_i]; } while (0)
#define GUEST_XMM_CVTDQ2PS_M(d, a) \
    do { xmm_t _s = ldx(a); int _i; \
         for (_i = 0; _i < 4; _i++) c->x[d].f[_i] = (float)_s.i32[_i]; } while (0)
#define GUEST_XMM_CVTDQ2PS0_R(d, s) GUEST_XMM_CVTDQ2PS_R(d, s)
#define GUEST_XMM_CVTDQ2PS0_M(d, a) GUEST_XMM_CVTDQ2PS_M(d, a)
#define GUEST_XMM_PSRLDQ(d, n) \
    do { xmm_t _r = (xmm_t){0}; unsigned _n = (n), _i; \
         for (_i = 0; _i + _n < 16; _i++) _r.u8[_i] = c->x[d].u8[_i + _n]; \
         c->x[d] = _r; } while (0)
#define GUEST_XMM_PSRA_IMM(bits, d, n) \
    do { unsigned _n = (n); int _i; \
         if (_n > (bits)) _n = (bits); \
         for (_i = 0; _i < 128 / (bits); _i++) c->x[d].i##bits[_i] >>= _n; } while (0)
#define GUEST_XMM_PSHL_IMM(bits, op, d, n) \
    do { unsigned _n = (n); int _i; \
         if (_n >= (bits)) { c->x[d] = (xmm_t){0}; } else \
         for (_i = 0; _i < 128 / (bits); _i++) c->x[d].u##bits[_i] op##= _n; } while (0)
#define GUEST_XMM_PSRA_REG(bits, d, s) \
    do { uint64_t _n = c->x[s].u64[0]; int _i; \
         if (_n > (bits) - 1) _n = (bits) - 1; \
         for (_i = 0; _i < 128 / (bits); _i++) c->x[d].i##bits[_i] >>= (unsigned)_n; } while (0)
#define GUEST_XMM_PSHL_REG(bits, op, d, s) \
    do { uint64_t _n = c->x[s].u64[0]; int _i; \
         if (_n >= (bits)) { c->x[d] = (xmm_t){0}; } else \
         for (_i = 0; _i < 128 / (bits); _i++) c->x[d].u##bits[_i] op##= (unsigned)_n; } while (0)
#define GUEST_XMM_PSHUFD_X(d, src, imm) \
    do { xmm_t _s = src, _r; unsigned _c = (imm); int _i; \
         for (_i = 0; _i < 4; _i++) _r.u32[_i] = _s.u32[(_c >> (_i*2)) & 3]; \
         c->x[d] = _r; } while (0)
#define GUEST_XMM_PSHUFD_R(d, s, imm) GUEST_XMM_PSHUFD_X(d, c->x[s], imm)
#define GUEST_XMM_PSHUFD_M(d, a, imm) GUEST_XMM_PSHUFD_X(d, ldx(a), imm)
#define GUEST_XMM_SHUFPS_X(d, src, imm) \
    do { xmm_t _d = c->x[d], _s = src, _r; unsigned _c = (imm); \
         _r.u32[0] = _d.u32[_c & 3]; _r.u32[1] = _d.u32[(_c>>2)&3]; \
         _r.u32[2] = _s.u32[(_c>>4)&3]; _r.u32[3] = _s.u32[(_c>>6)&3]; \
         c->x[d] = _r; } while (0)
#define GUEST_XMM_SHUFPS_R(d, s, imm) GUEST_XMM_SHUFPS_X(d, c->x[s], imm)
#define GUEST_XMM_SHUFPS_M(d, a, imm) GUEST_XMM_SHUFPS_X(d, ldx(a), imm)
#define GUEST_XMM_UNPCK_LANE_1(_r, _d, _s, _i, _k) \
    _r.u8[_i*2] = _d.u8[_k]; _r.u8[_i*2+1] = _s.u8[_k]
#define GUEST_XMM_UNPCK_LANE_2(_r, _d, _s, _i, _k) \
    _r.u16[_i*2] = _d.u16[_k]; _r.u16[_i*2+1] = _s.u16[_k]
#define GUEST_XMM_UNPCK_LANE_4(_r, _d, _s, _i, _k) \
    _r.u32[_i*2] = _d.u32[_k]; _r.u32[_i*2+1] = _s.u32[_k]
#define GUEST_XMM_UNPCK_LANE_8(_r, _d, _s, _i, _k) \
    _r.u64[_i*2] = _d.u64[_k]; _r.u64[_i*2+1] = _s.u64[_k]
#define GUEST_XMM_UNPCK_X(unit, hi, d, src) \
    do { xmm_t _d = c->x[d], _s = src, _r; int _i; \
         for (_i = 0; _i < (16 / (unit)) / 2; _i++) { \
             int _k = _i + ((hi) ? (16 / (unit)) / 2 : 0); \
             GUEST_XMM_UNPCK_LANE_##unit(_r, _d, _s, _i, _k); \
         } c->x[d] = _r; } while (0)
#define GUEST_XMM_UNPCK_R(unit, hi, d, s) GUEST_XMM_UNPCK_X(unit, hi, d, c->x[s])
#define GUEST_XMM_UNPCK_M(unit, hi, d, a) GUEST_XMM_UNPCK_X(unit, hi, d, ldx(a))
#define GUEST_XMM_PSHUFW_X(hi, d, src, imm) \
    do { xmm_t _s = src, _r = _s; unsigned _c = (imm); int _i; \
         for (_i = 0; _i < 4; _i++) \
             _r.u16[((hi) ? 4 : 0) + _i] = _s.u16[((hi) ? 4 : 0) + ((_c >> (_i * 2)) & 3U)]; \
         c->x[d] = _r; } while (0)
#define GUEST_XMM_PSHUFW_R(hi, d, s, imm) GUEST_XMM_PSHUFW_X(hi, d, c->x[s], imm)
#define GUEST_XMM_PSHUFW_M(hi, d, a, imm) GUEST_XMM_PSHUFW_X(hi, d, ldx(a), imm)
#define GUEST_XMM_PUNPCKLDQ_X(d, src) \
    do { xmm_t _d = c->x[d], _s = src, _r; \
         _r.u32[0] = _d.u32[0]; _r.u32[1] = _s.u32[0]; \
         _r.u32[2] = _d.u32[1]; _r.u32[3] = _s.u32[1]; \
         c->x[d] = _r; } while (0)
#define GUEST_XMM_PUNPCKLDQ_R(d, s) GUEST_XMM_PUNPCKLDQ_X(d, c->x[s])
#define GUEST_XMM_PUNPCKLDQ_M(d, a) GUEST_XMM_PUNPCKLDQ_X(d, ldx(a))
#define GUEST_XMM_COMIS_PAIRED(fld, d, rhs) \
    do { double _x = (double)c->x[d].fld[0], _y = (double)(rhs); \
         GUEST_FL->f_op = FLAG_PARTIAL; GUEST_FL->f_sz = 4; \
         GUEST_FL->f_sf = 0; GUEST_FL->f_of = 0; \
         if (_x != _x || _y != _y) { \
             GUEST_FL->f_cf = 1; GUEST_FL->f_zf = 1; GUEST_FL->f_pf = 1; \
         } else { \
             GUEST_FL->f_cf = (uint8_t)(_x < _y); \
             GUEST_FL->f_zf = (uint8_t)(_x == _y); GUEST_FL->f_pf = 0; \
         } } while (0)
#define GUEST_XMM_FCC(cc, x, y) cc_##cc(GUEST_FL)

/* ------------------------------------------ general registers as locals --- */
/* GUEST_GPR_LOCAL=0: registers directly in CPU struct */

#define GUEST_GPR_DECL ((void)0)
#define GR(reg) c->reg
/* Around every direct call the translator emits FLUSH/RELOAD. They are no-ops
 * here (registers live in the CPU struct), but with ISAAC_TRACE_ABI set they
 * turn into a callee-saved-register check: a callee that does not restore
 * esi/edi/ebx is an ABI violation that otherwise surfaces much later as a wild
 * pointer. See guest_abi_flush()/guest_abi_reload(). */
#define GUEST_GPR_FLUSH(c) guest_abi_flush(c)
#define GUEST_GPR_RELOAD(c) guest_abi_reload(c)
#define GPUSH(value) gpush_generated(c, value)
#define GPOP() gpop_generated(c)
#define GESP_SET(value) guest_stack_set_generated(c, value)
#define GESP_ADJ(amount) guest_stack_adjust_generated(c, amount)
#define GSTACK_ADDR(address, size) guest_stack_address_generated(c, address, size)

#ifdef __cplusplus
}
#endif
#endif /* ISAAC_GUEST_H */