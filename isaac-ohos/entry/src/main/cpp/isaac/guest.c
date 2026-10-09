/* Runtime for the recompiled guest: image mapping, dispatch, faults.
 * HarmonyOS/ARM64 adaptation of repentogxm's guest.c.
 *
 * Key adaptation: all guest memory access goes through the flat arena
 * (isaac_arena_base + addr) instead of direct host-address mapping.
 * The PE image is loaded into the arena at GUEST_IMAGE_BASE.
 * Stack is allocated within the arena.
 */
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <hilog/log.h>

#include "guest.h"
#include "guest_memory.h"

#define GL_ERR(fmt, ...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0, "isaacoh", fmt, ##__VA_ARGS__)
#define GL_INFO(fmt, ...) OH_LOG_Print(LOG_APP, LOG_INFO, 0, "isaacoh", fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------- globals ---- */

/* isaac_arena_base defined in guest_memory.c */
extern uint8_t *isaac_arena_base;

static void    *g_image_mem;
static uint32_t g_image_base, g_image_size;
static const guest_import *g_imports;
static uint32_t            g_import_count;
static void               *g_stack_mem;
static CPU                *g_stack_owner;
static uint32_t            g_stack_base, g_stack_top;
static unsigned char      *g_tib;

static const uint32_t *g_addrs;
static const guest_fn *g_fns;
static uint32_t        g_count;

unsigned char *g_guest_coverage_functions;
unsigned char *g_guest_coverage_imports;
unsigned char *g_guest_coverage_cases;

unsigned g_host_import_calls;
unsigned g_host_dynamic_calls;
uint32_t g_guest_fs_base_cached;
uint32_t g_guest_checkpoint_rva;
int g_guest_gl_inventory_mode;

static unsigned g_call_trace_count = 0;

/* How many guest_call entries get a full trace line before the log drops to
 * heartbeats. The default keeps a noisy startup from flooding the device log;
 * override with ISAAC_TRACE_MAX when localizing a fault (a large value plus
 * the heartbeat makes the last calls before a crash readable). */
static unsigned g_call_trace_max = 0;

static unsigned call_trace_max(void)
{
    if (g_call_trace_max == 0U) {
        const char *env = getenv("ISAAC_TRACE_MAX");
        unsigned long v = (env != NULL && env[0] != '\0')
                              ? strtoul(env, NULL, 0) : 200UL;
        g_call_trace_max = (v == 0UL) ? 1U : (unsigned)v;
    }
    return g_call_trace_max;
}

/* ISAAC_TRACE_ABI=1 makes guest_call verify that every callee restores the
 * callee-saved registers; off by default since it costs a check per call. */
static int g_abi_check = -1;

static int abi_check_enabled(void)
{
    if (g_abi_check < 0) {
        g_abi_check = (getenv("ISAAC_TRACE_ABI") != NULL) ? 1 : 0;
    }
    return g_abi_check;
}

/* Direct-call ABI check. The translator brackets every direct call with
 * GUEST_GPR_FLUSH/GUEST_GPR_RELOAD; with ISAAC_TRACE_ABI set these record the
 * callee-saved registers and compare them after the call returns, naming the
 * call site (the return address pushed just before it) when they differ. A
 * violation is a translation bug that otherwise surfaces hours later as a wild
 * pointer. */
typedef struct { uint32_t esi, edi, ebx, ret, esp; } guest_abi_frame;

#define GUEST_ABI_DEPTH 64U
static guest_abi_frame g_abi_frames[GUEST_ABI_DEPTH];
static unsigned        g_abi_depth;
static unsigned        g_abi_reports;

void guest_abi_flush(CPU *__restrict c)
{
    if (!abi_check_enabled()) {
        return;
    }
    if (g_abi_depth < GUEST_ABI_DEPTH) {
        guest_abi_frame *f = &g_abi_frames[g_abi_depth];
        f->esi = c->esi;
        f->edi = c->edi;
        f->ebx = c->ebx;
        f->ret = ld32(c->esp);   /* the call site pushed its return address */
        f->esp = c->esp;
    }
    g_abi_depth++;
}

void guest_abi_reload(CPU *__restrict c)
{
    if (!abi_check_enabled() || g_abi_depth == 0U) {
        return;
    }
    g_abi_depth--;
    if (g_abi_depth < GUEST_ABI_DEPTH) {
        const guest_abi_frame *f = &g_abi_frames[g_abi_depth];
        if (c->esp != f->esp) {
            if (g_abi_reports < 60U) {
                g_abi_reports++;
                GL_ERR("ABI: call site 0x%{public}08x returned with esp 0x%{public}08x "
                       "instead of 0x%{public}08x (drift %{public}d)",
                       f->ret, c->esp, f->esp, (int32_t)(c->esp - f->esp));
            }
            return;
        }
        if (c->esi != f->esi || c->edi != f->edi || c->ebx != f->ebx) {
            if (g_abi_reports < 40U) {
                g_abi_reports++;
                GL_ERR("ABI: call site 0x%{public}08x clobbered saved regs: "
                       "esi 0x%{public}08x->0x%{public}08x edi 0x%{public}08x->0x%{public}08x "
                       "ebx 0x%{public}08x->0x%{public}08x",
                       f->ret, f->esi, c->esi, f->edi, c->edi, f->ebx, c->ebx);
            }
        }
    }
}

#define GUEST_TLS_SLOTS 64U
#define GUEST_TLS_CALLBACKS_MAX 64U
static void               *g_tls_block;
static uint32_t            g_tls_block_size;
static uint32_t            g_tls_index = 0xFFFFFFFFu;
static uint32_t            g_tls_callbacks[GUEST_TLS_CALLBACKS_MAX];
static uint32_t            g_tls_callback_count;
static int                 g_tls_present;
static int                 g_tls_process_attached;

static int g_generation_noted;
static const char s_standalone_generation[] = "gen:standalone";

/* ----------------------------------------------------------- atomic ---- */

uint64_t guest_atomic_cmpxchg64(uint32_t addr, uint64_t expected, uint64_t desired)
{
    if (addr & 7U) {
        fprintf(stderr, "guest: unaligned LOCK CMPXCHG8B at %08x\n", addr);
        abort();
    }
    return __sync_val_compare_and_swap(
        (volatile uint64_t *)(isaac_arena_base + addr), expected, desired);
}

/* -------------------------------------------------------- generation ---- */

void guest_note_generation(const char *generation_id)
{
    if (!generation_id || strcmp(generation_id, s_standalone_generation) != 0) {
        fprintf(stderr, "guest: generated table/header identity mismatch\n");
        abort();
    }
    if (!g_generation_noted) {
        printf("guest: build %s\n", generation_id);
        g_generation_noted = 1;
    }
}

const char *guest_generation_id(void) { return s_standalone_generation; }

/* ---------------------------------------------------------- coverage ---- */

void guest_register_coverage_contract(const char *schema_id,
                                      const char *build_id,
                                      uint32_t function_count,
                                      uint32_t import_count,
                                      uint32_t case_count)
{
    (void)schema_id; (void)build_id;
    (void)function_count; (void)import_count; (void)case_count;
}

const char *guest_coverage_error(void) { return NULL; }

void guest_coverage_shutdown(void)
{
    g_guest_coverage_functions = NULL;
    g_guest_coverage_imports = NULL;
    g_guest_coverage_cases = NULL;
}

int guest_coverage_init_from_env(void)
{
    guest_coverage_shutdown();
    return 0;
}

/* ------------------------------------------------------------- image ---- */

static uint32_t image_rva(uint32_t addr)
{
    if (g_image_base && addr - g_image_base < g_image_size)
        return addr - g_image_base;
    return addr;
}

int guest_image_contains(uint32_t address, uint32_t size)
{
    return g_image_base && address >= g_image_base &&
           (uint64_t)address + size <= (uint64_t)g_image_base + g_image_size;
}

/* Load PE into the arena at GUEST_IMAGE_BASE.
 * This is a simplified loader: read the file, parse PE headers,
 * copy sections to their virtual addresses in the arena, apply relocations. */
int guest_image_load(const char *path)
{
    FILE *f;
    uint8_t *file_buf;
    uint32_t file_size;
    uint32_t e_lfanew, num_sections, opt_size;
    uint32_t section_table_offset;
    uint32_t orig_base;
    uint32_t i;

    if (g_image_mem || g_image_base || g_image_size) {
        GL_ERR("guest: refusing to replace a live guest image");
        return 1;
    }

    /* Read the entire file */
    f = fopen(path, "rb");
    if (!f) {
        GL_ERR("guest: cannot open %{public}s", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    file_size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    GL_INFO("guest: loading %{public}s size=%u", path, file_size);
    file_buf = (uint8_t *)malloc(file_size);
    if (!file_buf) {
        GL_ERR("guest: malloc failed for %u bytes", file_size);
        fclose(f);
        return 1;
    }
    if (fread(file_buf, 1, file_size, f) != file_size) {
        GL_ERR("guest: short read on %{public}s", path);
        free(file_buf);
        fclose(f);
        return 1;
    }
    fclose(f);

    /* Parse PE header */
    e_lfanew = *(uint32_t *)(file_buf + 0x3C);
    if (memcmp(file_buf + e_lfanew, "PE\0\0", 4) != 0) {
        GL_ERR("guest: bad PE signature at e_lfanew=%u", e_lfanew);
        free(file_buf);
        return 1;
    }

    num_sections = *(uint16_t *)(file_buf + e_lfanew + 6);
    opt_size = *(uint16_t *)(file_buf + e_lfanew + 20);
    orig_base = *(uint32_t *)(file_buf + e_lfanew + 4 + 20 + 28);
    g_image_size = *(uint32_t *)(file_buf + e_lfanew + 4 + 20 + 56);

    section_table_offset = e_lfanew + 4 + 20 + opt_size;

    /* Zero the image region in the arena */
    memset(isaac_arena_base + GUEST_IMAGE_BASE, 0, g_image_size);

    /* Copy headers */
    uint32_t header_size = *(uint32_t *)(file_buf + e_lfanew + 4 + 20 + 60);
    memcpy(isaac_arena_base + GUEST_IMAGE_BASE, file_buf,
           header_size < file_size ? header_size : file_size);

    /* Copy sections */
    for (i = 0; i < num_sections; i++) {
        uint8_t *sec = file_buf + section_table_offset + i * 40;
        uint32_t virtual_addr = *(uint32_t *)(sec + 12);
        uint32_t raw_size = *(uint32_t *)(sec + 16);
        uint32_t raw_addr = *(uint32_t *)(sec + 20);

        if (raw_size == 0) continue;

        uint32_t dst = GUEST_IMAGE_BASE + virtual_addr;
        uint32_t src = raw_addr;
        uint32_t copy_size = raw_size;

        if (src + copy_size > file_size) copy_size = file_size - src;
        if (dst + copy_size > GUEST_IMAGE_BASE + g_image_size)
            copy_size = GUEST_IMAGE_BASE + g_image_size - dst;

        memcpy(isaac_arena_base + dst, file_buf + src, copy_size);
    }

    /* Apply relocations (HIGHLOW type only) */
    uint32_t reloc_rva = *(uint32_t *)(file_buf + e_lfanew + 4 + 20 + 136);
    uint32_t reloc_size = *(uint32_t *)(file_buf + e_lfanew + 4 + 20 + 140);
    if (reloc_rva && reloc_size) {
        uint32_t delta = GUEST_IMAGE_BASE - orig_base;
        uint8_t *reloc_data = file_buf + /* need to find raw offset of reloc_rva */0;
        GL_INFO("guest: reloc_rva=0x%{public}08x reloc_size=%{public}u delta=0x%{public}08x orig_base=0x%{public}08x",
               reloc_rva, reloc_size, delta, orig_base);
        /* For simplicity, find the raw offset by scanning sections */
        for (i = 0; i < num_sections; i++) {
            uint8_t *sec = file_buf + section_table_offset + i * 40;
            uint32_t va = *(uint32_t *)(sec + 12);
            uint32_t rs = *(uint32_t *)(sec + 16);
            uint32_t ra = *(uint32_t *)(sec + 20);
            if (reloc_rva >= va && reloc_rva < va + rs) {
                reloc_data = file_buf + ra + (reloc_rva - va);
                break;
            }
        }
        if (reloc_data) {
            uint32_t offset = 0;
            uint32_t fix_count = 0;
            while (offset < reloc_size) {
                uint32_t page_rva = *(uint32_t *)(reloc_data + offset);
                uint32_t block_size = *(uint32_t *)(reloc_data + offset + 4);
                if (block_size == 0) break;
                uint32_t entry_count = (block_size - 8) / 2;
                for (i = 0; i < entry_count; i++) {
                    uint16_t entry = *(uint16_t *)(reloc_data + offset + 8 + i * 2);
                    uint16_t type = entry >> 12;
                    uint16_t off = entry & 0xFFF;
                    if (type == 3) { /* HIGHLOW */
                        uint32_t fix_addr = GUEST_IMAGE_BASE + page_rva + off;
                        uint32_t old_val = *(uint32_t *)(isaac_arena_base + fix_addr);
                        *(uint32_t *)(isaac_arena_base + fix_addr) = old_val + delta;
                        fix_count++;
                    }
                }
                offset += block_size;
            }
            GL_INFO("guest: applied %{public}u relocations", fix_count);
        } else {
            GL_ERR("guest: could not find reloc section in file");
        }
    } else {
        GL_ERR("guest: no relocation table (rva=%{public}08x size=%{public}u)", reloc_rva, reloc_size);
    }

    g_image_base = GUEST_IMAGE_BASE;
    g_image_mem = file_buf; /* keep file buffer for now */

    GL_INFO("guest: image loaded at %08x, size %08x, %u sections",
           g_image_base, g_image_size, num_sections);
    return 0;
}

void guest_image_free(void)
{
    if (g_tls_block) free(g_tls_block);
    g_tls_block = NULL;
    g_guest_fs_base_cached = 0U;
    g_tls_block_size = 0U;
    g_tls_index = 0xFFFFFFFFu;
    g_tls_callback_count = 0U;
    g_tls_present = 0;
    g_tls_process_attached = 0;
    if (g_image_mem) free(g_image_mem);
    g_image_mem = NULL;
    g_image_base = 0;
    g_image_size = 0;
}

/* ------------------------------------------------------------- stack ---- */

void guest_cpu_init(CPU *c)
{
    if (c) memset(c, 0, sizeof *c);
}

int guest_stack_bind(CPU *c, uint32_t floor, uint32_t ceiling)
{
    if (!c || floor >= ceiling) return 1;
    c->stack_owner = c;
    c->stack_floor = floor;
    c->stack_ceiling = ceiling;
    c->stack_low_water = ceiling;
    return 0;
}

int guest_stack_init(CPU *c)
{
    uint32_t base, floor, ceiling;
#define GUEST_STACK_LOW_MARGIN 4096U
#define GUEST_STACK_TOP_MARGIN 4096U

    if (!c || g_stack_mem) return 1;

    /* Allocate stack within the arena — use high addresses to avoid
     * collision with the image at 0x30000000 */
    base = 0x80000000U;
    floor = base + GUEST_STACK_LOW_MARGIN;
    ceiling = base + GUEST_STACK_SIZE - GUEST_STACK_TOP_MARGIN;

    memset(isaac_arena_base + base, 0, GUEST_STACK_SIZE);

    if (guest_stack_bind(c, floor, ceiling) != 0) {
        return 1;
    }

    c->esp = ceiling;
    c->ebp = ceiling;
    c->fault = NULL;
    c->fault_addr = 0;
    c->exit_api = NULL;
    c->exit_code = 0;
    c->stop_kind = GUEST_RUN_RETURNED;
    c->run_scope = NULL;
    c->jump_sites = NULL;
    c->jump_value = 0U;
    g_stack_mem = (void *)(uintptr_t)base; /* marker */
    g_stack_owner = c;
    g_stack_base = c->stack_floor;
    g_stack_top = c->stack_ceiling;
    g_guest_fs_base_cached = 0U;
#undef GUEST_STACK_LOW_MARGIN
#undef GUEST_STACK_TOP_MARGIN
    return 0;
}

void guest_stack_free(CPU *c)
{
    g_stack_mem = NULL;
    g_stack_owner = NULL;
    g_stack_base = g_stack_top = 0;
    g_guest_fs_base_cached = 0U;
    if (c && c->stack_owner == c) {
        c->esp = c->ebp = 0;
        c->jump_sites = NULL;
        c->jump_value = 0U;
        c->stack_owner = NULL;
        c->stack_floor = 0U;
        c->stack_ceiling = 0U;
        c->stack_low_water = 0U;
    }
}

/* ---------------------------------------------------------- dispatch ---- */

void guest_register(const uint32_t *addrs, const guest_fn *fns, uint32_t n)
{
    g_addrs = addrs; g_fns = fns; g_count = n;
}

const uint32_t *guest_registered_addresses(uint32_t *count)
{
    if (count) *count = g_count;
    return g_addrs;
}

/* HarmonyOS ships no Windows loader, so nothing fills the import address
 * table. The translator's dispatch expects every IAT slot to hold its own
 * guest address as a self-identifying token: guest_import_lookup() maps a
 * value back through image_rva() and compares it against slot_rva.
 *
 * Left with their file contents, the slots still hold the RVA of each
 * import's IMAGE_IMPORT_BY_NAME record, so `call dword ptr [slot]` jumps into
 * the import name strings instead of a handler -- observed as a call with
 * address 0x007a8dda, which is the name entry of KERNEL32!GetSystemTimeAsFileTime. */
void guest_register_imports(const guest_import *imports, uint32_t n)
{
    uint32_t i;
    uint32_t filled = 0;
    uint32_t sample_slot = 0, sample_before = 0, sample_after = 0;

    g_imports = imports;
    g_import_count = n;

    for (i = 0; i < n; i++) {
        uint32_t token = GUEST_IMAGE_BASE + imports[i].slot_rva;
        uint32_t before = ld32(token);
        if (before != token) {
            if (filled == 0) {
                sample_slot = imports[i].slot_rva;
                sample_before = before;
            }
            st32(token, token);
            filled++;
            if (filled == 1) {
                sample_after = ld32(token);
            }
        }
    }
    GL_INFO("guest_register_imports: %u slots, %u filled (first 0x%{public}06x: 0x%{public}08x -> 0x%{public}08x)",
            n, filled, sample_slot, sample_before, sample_after);
    /* The KAGE file-reader vtable is 15 methods long, but the image loader
     * calls +0x6c after decoding (out-of-bounds read into GLFW string data
     * = 0x4e -> untranslated jump). Point that slot at a succeed-always
     * import token; see h_pack_vtable_stub. */
    st32(0x30764894U + 0x6cU, 0x306064b0U);
}

/* Re-fill every IAT slot with its self-identifying token. The engine's
 * archive-mount pass has been observed scribbling over slots (e.g.
 * WINMM!timeGetTime at 0x306064c0 ended up holding 0x3075aa44, the import's
 * hint/name RVA), which turns the next `call edi` through that slot into a
 * jump into .rdata. Host code calls this after the mount-table probe to
 * restore all tokens in one go. */
void guest_reregister_imports(void)
{
    if (g_imports != 0 && g_import_count != 0) {
        /* The engine installs its own hooks into IAT slots after startup
         * (delay-load style: a slot ends up holding a guest code pointer, as
         * observed for CRT-stdio slots the archive VFS wraps). Restoring
         * those to our tokens destroys the hook and misroutes the call to
         * the static import name. Preserve any slot whose current value is a
         * guest CODE address we hold a translation for; restore the rest. */
        uint32_t i, kept = 0, restored = 0;
        uint32_t keep_rva_sample = 0;

        for (i = 0; i < g_import_count; i++) {
            uint32_t token = GUEST_IMAGE_BASE + g_imports[i].slot_rva;
            uint32_t before = ld32(token);
            if (before == token) {
                continue;
            }
            if (before >= GUEST_IMAGE_BASE + 0x1000U &&
                before < GUEST_IMAGE_BASE + 0x606000U &&
                guest_lookup(before) != NULL) {
                kept++;
                if (keep_rva_sample == 0U) {
                    keep_rva_sample = g_imports[i].slot_rva;
                }
                continue;
            }
            st32(token, token);
            restored++;
        }
        GL_INFO("guest_reregister_imports: %u slots, %u kept (game hooks, "
                "first rva 0x%{public}06x), %u restored to tokens",
                g_import_count, kept, keep_rva_sample, restored);
    }
}

static const guest_import *guest_import_lookup(uint32_t slot_addr,
                                               uint32_t *id_out)
{
    uint32_t key = image_rva(slot_addr);
    uint32_t lo = 0, hi = g_import_count;
    if (!hi || key < g_imports[0].slot_rva ||
        key > g_imports[hi - 1U].slot_rva)
        return NULL;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (g_imports[mid].slot_rva == key) {
            if (id_out) *id_out = mid;
            return &g_imports[mid];
        }
        if (g_imports[mid].slot_rva < key) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

const char *guest_import_name(uint32_t slot_addr)
{
    const guest_import *imported = guest_import_lookup(slot_addr, NULL);
    return imported ? imported->name : NULL;
}

guest_fn guest_lookup(uint32_t addr)
{
    addr = image_rva(addr);
    uint32_t lo = 0, hi = g_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (g_addrs[mid] == addr) return g_fns[mid];
        if (g_addrs[mid] < addr) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

void guest_call(CPU *__restrict c, uint32_t addr)
{
    uint32_t import_id = 0U;
    guest_fn fn;

    g_call_trace_count++;

    if (g_call_trace_count <= call_trace_max()) {
        GL_INFO("guest_call[%u]: addr=0x%{public}08x esp=0x%{public}08x ebp=0x%{public}08x",
               g_call_trace_count, addr, c->esp, c->ebp);
    } else if (g_call_trace_count == call_trace_max() + 1U) {
        GL_INFO("guest_call: trace limit reached, switching to heartbeat mode");
    }

    if ((g_call_trace_count & 0x3FFU) == 0U) {
        GL_INFO("guest_call heartbeat: count=%{public}u esp=0x%{public}08x ebp=0x%{public}08x",
               g_call_trace_count, c->esp, c->ebp);
    }


    const guest_import *imported = guest_import_lookup(addr, &import_id);
    if (imported) {
        if (g_call_trace_count <= call_trace_max()) {
            GL_INFO("guest_call[%u]: -> import %{public}s", g_call_trace_count, imported->name);
        }
        guest_coverage_import(import_id);
        ++g_host_import_calls;
        if (guest_host_import(c, imported->name))
            return;
        guest_fault(c, addr, imported->name);
        return;
    }

    fn = guest_lookup(addr);
    if (fn) {
        if (g_call_trace_count <= call_trace_max()) {
            GL_INFO("guest_call[%u]: -> guest fn rva=0x%{public}08x", g_call_trace_count, image_rva(addr));
        }
        if (abi_check_enabled()) {
            /* ISAAC_TRACE_ABI: a callee that does not restore esi/edi/ebx is a
             * translation or shim bug that surfaces much later as a wild
             * pointer; name it here instead. */
            uint32_t esi = c->esi;
            uint32_t edi = c->edi;
            uint32_t ebx = c->ebx;
            uint32_t esp = c->esp;
            fn(c);
            if (c->esp != esp) {
                GL_ERR("ABI: callee rva=0x%{public}08x returned with esp 0x%{public}08x "
                       "instead of 0x%{public}08x (drift %{public}d) ret=0x%{public}08x",
                       image_rva(addr), c->esp, esp, (int32_t)(c->esp - esp), ld32(esp));
            }
            if (c->esi != esi || c->edi != edi || c->ebx != ebx) {
                GL_ERR("ABI: callee rva=0x%{public}08x broke saved regs: "
                       "esi 0x%{public}08x->0x%{public}08x edi 0x%{public}08x->0x%{public}08x "
                       "ebx 0x%{public}08x->0x%{public}08x esp=0x%{public}08x",
                       image_rva(addr), esi, c->esi, edi, c->edi, ebx, c->ebx, c->esp);
            }
            return;
        }
        fn(c);
        return;
    }

    /* Native dynamic token (GetProcAddress-style) */
    if (addr >= 0x7d000000U && addr <= 0x7effffffU) {
        ++g_host_dynamic_calls;
        if (guest_host_dynamic(c, addr))
            return;
    }

    /* Try translating an unrelocated PE VA to an arena address.
     * Some indirect calls read function pointers from memory before
     * relocations are applied to those locations (e.g. TLS callbacks,
     * vtables, or IAT thunks that the simplified loader missed). */
    if (addr >= 0x00400000U && addr < 0x00400000U + g_image_size) {
        uint32_t rva = addr - 0x00400000U;
        GL_INFO("guest_call: trying translate PE VA 0x%{public}08x (rva=0x%{public}08x) esp=0x%{public}08x ebp=0x%{public}08x",
               addr, rva, c->esp, c->ebp);
        /* guest_addresses[] stores RVAs, so look up the RVA directly */
        fn = guest_lookup(rva);
        if (fn) {
            GL_INFO("guest_call: translated 0x%{public}08x -> rva 0x%{public}08x", addr, rva);
            fn(c);
            return;
        }
        imported = guest_import_lookup(rva + GUEST_IMAGE_BASE, &import_id);
        if (imported) {
            guest_coverage_import(import_id);
            ++g_host_import_calls;
            GL_INFO("guest_call: translated import 0x%{public}08x name=%{public}s", addr, imported->name);
            if (guest_host_import(c, imported->name))
                return;
            guest_fault(c, addr, imported->name);
            return;
        }
        /* Try finding the nearest function entry point before this RVA. */
        {
            uint32_t lo = 0, hi = g_count;
            while (lo < hi) {
                uint32_t mid = lo + (hi - lo) / 2;
                if (g_addrs[mid] <= rva) lo = mid + 1;
                else hi = mid;
            }
            if (lo > 0) {
                uint32_t best_rva = g_addrs[lo - 1];
                uint32_t offset = rva - best_rva;
                if (offset < 0x1000U) {
                    /* Loud on purpose. Entering a function at a non-zero
                     * offset means the pointer we were handed is almost
                     * certainly wrong; jumping to the nearest entry hides
                     * that behind arbitrary behaviour and costs hours of
                     * "guest went silent" debugging. */
                    GL_ERR("guest_call: SUSPECT pointer 0x%{public}08x -> nearest rva 0x%{public}08x (offset +0x%{public}x); entering it anyway",
                           addr, best_rva, offset);
                    g_fns[lo - 1](c);
                    return;
                }
            }
        }
    }

    guest_fault(c, addr, "untranslated address");
}

/* ---------------------------------------------------------- run/fault ---- */

typedef struct guest_run_scope {
    jmp_buf env;
} guest_run_scope;

int guest_run_until_stop(CPU *__restrict c, guest_fn entry)
{
    guest_run_scope scope;
    int prev_kind;

    if (!c || !entry || c->run_scope) return GUEST_RUN_INVALID;

    prev_kind = c->stop_kind;
    c->run_scope = &scope;
    c->stop_kind = GUEST_RUN_RETURNED;
    c->fault = NULL;
    c->exit_api = NULL;

    if (setjmp(scope.env) == 0) {
        entry(c);
        c->stop_kind = GUEST_RUN_RETURNED;
    }

    c->run_scope = NULL;
    return c->stop_kind;
}

/* Name the guest function that contains `rva`: the nearest dispatch entry at
 * or below it, plus the offset into it. Translated code is native C, so host
 * frames carry no guest PCs -- this window is how a fault gets attributed
 * back to guest code. */
static int guest_name_rva(uint32_t rva, uint32_t *base_out, uint32_t *offset_out)
{
    uint32_t lo = 0;
    uint32_t hi = g_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2U;
        if (g_addrs[mid] <= rva) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    if (lo == 0U) {
        return 0;
    }
    if (base_out != NULL) {
        *base_out = g_addrs[lo - 1U];
    }
    if (offset_out != NULL) {
        *offset_out = rva - g_addrs[lo - 1U];
    }
    return 1;
}

/* Dump the guest stack window on fault: any dword pointing into the image is a
 * stale return RVA, so the window names the chain that led here.
 *
 * Two spellings appear on the stack and both must be accepted: the generated
 * code pushes call-site return addresses as bare RVAs (e.g. 0x005eb77a), while
 * data-derived pointers carry the image base (0x30xxxxxx). Filtering on the
 * base alone silently drops exactly the words that identify the fault site. */
static void guest_dump_fault_stack(const CPU *c)
{
    uint32_t i;

    GL_ERR("guest_fault: esp=0x%{public}08x ebp=0x%{public}08x eax=0x%{public}08x",
           c->esp, c->ebp, c->eax);

    /* Globals that gate lazily-constructed singletons: when a virtual call
     * lands on NULL the useful question is whether the object was never
     * built, and which flag suppressed its construction. */
    {
        uint32_t gate_obj = ld32(0x307fd680U);
        GL_ERR("  gate obj [0x307fd680]=0x%{public}08x  singleton [0x307fece4]=0x%{public}08x",
               gate_obj, ld32(0x307fece4U));
        if (gate_obj != 0U && gate_obj + 0x29e77U < 0xF0000000U) {
            GL_ERR("  gate byte [[0x307fd680]+0x29e77] = %{public}u",
                   (unsigned)ld8(gate_obj + 0x29e77U));
        } else {
            GL_ERR("  gate obj is NULL or out of range, no gate byte to read");
        }
    }

    /* The three early-outs that leave the 0x307fece4 singleton unbuilt all
     * branch to the same label, so print each one's condition. Whichever is
     * zero is the branch that fired.
     *   [ebp-0x24]      object produced by the factory at RVA 0x4b3340
     *   [0x307fd62c]    a second optional callback pointer
     * If BOTH are non-zero, the branch taken was the virtual call at
     * RVA 0x3a938a returning false. */
    if (c->ebp > 0x1000U) {
        GL_ERR("  early-out probes: [ebp-0x24]=0x%{public}08x [0x307fd62c]=0x%{public}08x",
               ld32(c->ebp - 0x24U), ld32(0x307fd62cU));
    }

    if (g_image_size == 0U || c->esp == 0U) {
        return;
    }
    for (i = 0; i < 32U; i++) {
        uint32_t a = c->esp + i * 4U;
        uint32_t v = ld32(a);
        uint32_t rva = 0;
        uint32_t base = 0;
        uint32_t off = 0;
        int is_code = 0;

        if (v >= GUEST_IMAGE_BASE && v < GUEST_IMAGE_BASE + g_image_size) {
            rva = v - GUEST_IMAGE_BASE;
            is_code = 1;
        } else if (v >= 0x1000U && v < g_image_size) {
            rva = v; /* bare RVA pushed by generated call sites */
            is_code = 1;
        }
        if (!is_code) {
            continue;
        }
        if (guest_name_rva(rva, &base, &off)) {
            GL_ERR("  [esp+0x%{public}02x]=0x%{public}08x rva=0x%{public}08x in fn 0x%{public}08x +0x%{public}x",
                   i * 4U, v, rva, base, off);
        }
    }
}

void guest_fault(CPU *__restrict c, uint32_t addr, const char *what)
{
    if (!c) abort();
    c->fault = what;
    c->fault_addr = addr;
    c->stop_kind = GUEST_RUN_FAULT;
    GL_ERR("guest_fault: addr=0x%{public}08x what=%{public}s", addr, what ? what : "?");
    guest_dump_fault_stack(c);
    if (c->run_scope) longjmp(((guest_run_scope *)c->run_scope)->env, 1);
    abort();
}

void guest_exit(CPU *__restrict c, int32_t code, const char *api)
{
    if (!c) abort();
    c->exit_api = api;
    c->exit_code = code;
    c->stop_kind = GUEST_RUN_EXIT;
    if (c->run_scope) longjmp(((guest_run_scope *)c->run_scope)->env, 1);
    fprintf(stderr, "guest: exit(%d) via %s\n", code, api ? api : "?");
    abort();
}

void guest_int3(CPU *__restrict c, uint32_t addr)
{
    guest_fault(c, addr, "int3");
}

int guest_checkpoint(CPU *__restrict c, uint32_t rva)
{
    (void)c; (void)rva;
    return 0;
}

/* ----------------------------------------------------------- setjmp ---- */

void guest_setjmp_prepare(CPU *__restrict c, guest_jump_site *site)
{
    site->previous = c->jump_sites;
    site->guest_esp = c->esp;
    site->guest_eip = 0;
    /* The guest jmp_buf address is _setjmp3's first argument, sitting on the
     * guest stack right above the return address gpush_at pushed. Record it
     * so a later longjmp(buf, ...) can find this site. */
    site->guest_env = ld32(c->esp + 4U);
    c->jump_sites = site;
}

void guest_setjmp_finish(CPU *__restrict c, guest_jump_site *site, int resumed)
{
    if (resumed) {
        c->esp = site->guest_esp;
        /* longjmp(buf, val) makes setjmp "return" val (never 0) */
        c->eax = c->jump_value;
    } else {
        /* direct _setjmp3 return is 0 */
        c->eax = 0U;
    }
    if (c->jump_sites == site)
        c->jump_sites = site->previous;
}

void guest_setjmp_leave(CPU *__restrict c, guest_jump_site *site)
{
    if (c->jump_sites == site)
        c->jump_sites = site->previous;
}

GUEST_NORETURN void guest_longjmp(CPU *__restrict c, uint32_t guest_env, int32_t value)
{
    guest_jump_site *site = c->jump_sites;
    while (site) {
        if (site->guest_env == guest_env) {
            c->jump_value = (uint32_t)value;
            longjmp(site->native_env, value ? value : 1);
        }
        site = site->previous;
    }
    guest_fault(c, guest_env, "longjmp without matching setjmp");
}

/* ------------------------------------------------------- fs base / cpuid ---- */

uint32_t guest_fs_base(CPU *__restrict c)
{
    (void)c;
    /* Return a synthetic TEB address in the arena */
    if (!g_tib) {
        g_tib = isaac_arena_base + 0x7f000000U;
        memset(g_tib, 0, 4096);
    }
    uint32_t base = (uint32_t)(g_tib - isaac_arena_base);
    g_guest_fs_base_cached = base;
    return base;
}

void guest_cpuid(CPU *__restrict c)
{
    /* Minimal CPUID emulation: report as a generic x86 with SSE/SSE2 */
    uint32_t leaf = c->eax;
    switch (leaf) {
    case 0: /* vendor */
        c->eax = 2;
        c->ebx = 0x756E6547; /* "Genu" */
        c->ecx = 0x6C65746E; /* "ntel" */
        c->edx = 0x49656E69; /* "ineI" */
        break;
    case 1: /* features */
        c->eax = 0x00000600;
        c->ebx = 0;
        c->ecx = 0;
        c->edx = 0x00000000; /* no features for now */
        break;
    default:
        c->eax = c->ebx = c->ecx = c->edx = 0;
        break;
    }
}

void guest_xgetbv(CPU *__restrict c)
{
    c->eax = 0;
    c->edx = 0;
}

/* ------------------------------------------------------- host import stubs ---- */
/* guest_host_import and guest_host_dynamic are implemented in host_ohos.c:
 * the dynamic dispatcher must live next to the token table that issues the
 * tokens, or the two can drift and hand the guest an unreachable address. */

/* guest_kage_pc_handoff_ready() lives in host_kage.c (upstream: manual_kage.c):
 * the KAGE platform seam owns the "backend ready" commit point that the
 * generated handoff bypasses read, and a second definition here would be a
 * duplicate symbol. */

/* ------------------------------------------------------- TLS ---- */

int guest_tls_process_attach(CPU *__restrict c)
{
    (void)c;
    g_tls_process_attached = 1;
    return 0;
}