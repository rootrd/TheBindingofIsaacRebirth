import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\guest.c'
src = io.open(p, encoding='utf-8', newline='').read()
anchor = """static int abi_check_enabled(void)
{
    if (g_abi_check < 0) {
        g_abi_check = (getenv("ISAAC_TRACE_ABI") != NULL) ? 1 : 0;
    }
    return g_abi_check;
}
"""
add = anchor + """
/* Direct-call ABI check. The translator brackets every direct call with
 * GUEST_GPR_FLUSH/GUEST_GPR_RELOAD; with ISAAC_TRACE_ABI set these record the
 * callee-saved registers and compare them after the call returns, naming the
 * call site (the return address pushed just before it) when they differ. A
 * violation is a translation bug that otherwise surfaces hours later as a wild
 * pointer. */
typedef struct { uint32_t esi, edi, ebx, ret; } guest_abi_frame;

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
"""
assert anchor in src, "anchor not found"
io.open(p, 'w', encoding='utf-8', newline='').write(src.replace(anchor, add, 1))
print("patched guest.c")
