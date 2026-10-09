import io, re

GEN = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated'

# ---- 1) the real fix ------------------------------------------------------
p = GEN + r'\guest_0166.c'
src = io.open(p, encoding='utf-8', newline='').read()
old = """    { extern int isaac_hook_resolve_path(CPU *__restrict c);
      if (isaac_hook_resolve_path(c)) {
        GUEST_GPR_FLUSH(c);
        return;
      } }"""
new = """    { extern int isaac_hook_resolve_path(CPU *__restrict c);
      if (isaac_hook_resolve_path(c)) {
        /* The native body ends in `ret 4`: the callee drops the return
         * address and its one stack argument. Returning from the hook without
         * that leaves 8 bytes on the caller's stack; every later pop in the
         * caller is then shifted, so the resource-load path reads a garbage
         * value into esi and the mods singleton never becomes non-NULL (the
         * NULL virtual call at 0x3a93ba). */
        c->esp += 8U;
        GUEST_GPR_FLUSH(c);
        return;
      } }"""
assert src.count(old) == 1, src.count(old)
src = src.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(src)
print("fixed the resolve-path hook stack cleanup")

# ---- 2) remove the temporary debug instrumentation ------------------------
for f in ('guest_0144.c', 'guest_0166.c', 'guest_0167.c'):
    p = GEN + '\\' + f
    s = io.open(p, encoding='utf-8', newline='').read()
    lines = s.split('\n')
    kept = [ln for ln in lines if 'ISAAC_DBG_CALL' not in ln]
    s = '\n'.join(kept)
    s = s.replace('#include <stdio.h>\n#include <stdlib.h>\n#include "guest.h"', '#include "guest.h"')
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print("cleaned", f)
