/* Extension points for guest import handlers implemented in separate
 * translation units.
 *
 * host_ohos.c owns the dispatch and the bulk of the shims. Modules that are
 * large or developed independently (the CRT printf formatter, float-return
 * math, fiber/TLS storage) live in their own file and publish a descriptor
 * here, so parallel work never edits the same file.
 *
 * Lookup order in host_ohos.c: every module first, in the order listed by
 * s_ext_providers, then the generated table, then s_host_overrides. A module
 * may therefore also replace an entry the generated table routes to hDUMMY.
 */
#ifndef HOST_EXT_H
#define HOST_EXT_H

#include "guest.h"

typedef struct {
    /* "DLL!Function", spelled exactly as in generated/host_imports_table.h */
    const char *name;
    void (*fn)(CPU *c);
} host_ext_entry;

typedef struct {
    const host_ext_entry *entries;   /* statically imported names */
    unsigned entry_count;
    const host_ext_entry *dynamic;   /* names resolved via GetProcAddress */
    unsigned dynamic_count;
} host_ext_module;

/* Return a pointer to a static descriptor, or NULL when the module is absent.
 * Implementations must never return a descriptor with a NULL entries/dynamic
 * pointer paired with a non-zero count.
 *
 * ORDER IS LOAD-BEARING: the provider slot index (1-based) is encoded into the
 * dynamic tokens a module hands out, so moving a module in host_ohos.c's
 * s_ext_providers[] invalidates every token it already issued. host_ext_kage
 * must stay at slot 1 (tokens 0x7d02xxxx). */
const host_ext_module *host_ext_printf(void);
const host_ext_module *host_ext_kage(void);
const host_ext_module *host_ext_misc(void);
const host_ext_module *host_ext_file(void);
const host_ext_module *host_ext_path(void);

/* Token -> native FILE* bridge for the fopen() tokens host_ext_file issues.
 * Sibling modules (the vfprintf handler in host_ext_printf.c) resolve a guest
 * FILE token through this so writes land in the real stream; NULL when the
 * token does not name a live slot. */
struct _IO_FILE;
struct _IO_FILE *host_ext_file_stream(uint32_t token);

/* Rebuild the engine's volume container (0x307c7b30) with the root,
 * "resources/" and "resources-dlc3/" volumes after the registry lookup
 * failed to produce them. Called from host_ext_file.c's fopen shim once the
 * second kage_mount_points.dat probe fails. */
void isaac_mount_container_patch(void);

#endif /* HOST_EXT_H */
