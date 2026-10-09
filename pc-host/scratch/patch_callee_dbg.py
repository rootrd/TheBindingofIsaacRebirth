import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0167.c'
src = io.open(p, encoding='utf-8', newline='').read()

DBG = ('    if (getenv("ISAAC_DBG_CALL")) fprintf(stderr, '
       '"[DBG56c7e0 TAG] esi=0x%08x edi=0x%08x ebx=0x%08x esp=0x%08x ebp=0x%08x\\n", '
       '(unsigned)c->esi, (unsigned)c->edi, (unsigned)c->ebx, (unsigned)c->esp, (unsigned)c->ebp);\n')

markers = [
    ("    /* 0056c7e1  mov ebp, esp */\n    GR(ebp) = (uint32_t)(GR(esp));\n", "entry"),
    ("    /* 0056cc7b  pop ecx */\n", "before-pops"),
    ("    /* 0056cc7f  mov esp, ebp */\n", "after-pops"),
]

for marker, tag in markers:
    assert src.count(marker) == 1, (tag, src.count(marker))
    src = src.replace(marker, marker + DBG.replace("TAG", tag), 1)

if '#include <stdio.h>' not in src:
    src = src.replace('#include "guest.h"', '#include <stdio.h>\n#include <stdlib.h>\n#include "guest.h"', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(src)
print("instrumented sub_0056c7e0")
