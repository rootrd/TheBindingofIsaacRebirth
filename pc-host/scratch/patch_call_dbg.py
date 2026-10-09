import io

# 1) caller side: right after the call to sub_00563120
pc = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0167.c'
src = io.open(pc, encoding='utf-8', newline='').read()
marker = "    GPUSH(0x56c82bU); GUEST_STACK_CALLSITE_BARRIER(); GUEST_GPR_FLUSH(c); sub_00563120(c); GUEST_GPR_RELOAD(c);\n"
assert src.count(marker) == 1, src.count(marker)
dbg = ('    if (getenv("ISAAC_DBG_CALL")) fprintf(stderr, '
       '"[DBG after 563120] esp=0x%08x ebp=0x%08x [esp]=0x%08x [esp+4]=0x%08x\\n", '
       '(unsigned)c->esp, (unsigned)c->ebp, (unsigned)ld32(c->esp), (unsigned)ld32(c->esp + 4));\n')
src = src.replace(marker, marker + dbg, 1)
io.open(pc, 'w', encoding='utf-8', newline='').write(src)

# 2) callee side: entry and every return-ish point of sub_00563120
pe = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0166.c'
cs = io.open(pe, encoding='utf-8', newline='').read()
entry = "void sub_00563120(CPU *__restrict c)\n{\n"
assert cs.count(entry) == 1
cs = cs.replace(entry, entry + '    if (getenv("ISAAC_DBG_CALL")) fprintf(stderr, "[DBG 563120 entry] esp=0x%08x [esp]=0x%08x [esp+4]=0x%08x\\n", (unsigned)c->esp, (unsigned)ld32(c->esp), (unsigned)ld32(c->esp + 4));\n', 1)
ret = "    /* 00563183  ret 4 */\n"
assert cs.count(ret) == 1, cs.count(ret)
cs = cs.replace(ret, '    if (getenv("ISAAC_DBG_CALL")) fprintf(stderr, "[DBG 563120 at-ret] esp=0x%08x\\n", (unsigned)c->esp);\n' + ret, 1)
if '#include <stdio.h>' not in cs:
    cs = cs.replace('#include "guest.h"', '#include <stdio.h>\n#include <stdlib.h>\n#include "guest.h"', 1)
io.open(pe, 'w', encoding='utf-8', newline='').write(cs)
print("instrumented the 0x56c826 call and sub_00563120")
