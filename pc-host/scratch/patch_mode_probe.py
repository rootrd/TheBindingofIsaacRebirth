import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0172.c'
s = io.open(p, encoding='utf-8', newline='').read()

marker = "    /* 0059c39e  mov al, byte ptr [edx + 0x10] */\n"
assert s.count(marker) == 1, s.count(marker)
dbg = ('    if (getenv("ISAAC_DBG_BUFFER")) { static int dn_ = 0; if (dn_ < 4) { dn_++;\n'
       '        fprintf(stderr, "[DBG mode] edi=%08x decoder=%08x mode=%d cl=%08x esi=%08x\\n",\n'
       '                (unsigned)GR(edi), (unsigned)GR(edx),\n'
       '                (int)ld8(GR(edx) + 0x10U),\n'
       '                (unsigned)ld32((unsigned)(GR(ebp) + (int)(-148))),\n'
       '                (unsigned)ld32((unsigned)(GR(ebp) + (int)(-136)))); } }\n')
s = s.replace(marker, dbg + marker, 1)
if '#include <stdio.h>' not in s:
    s = s.replace('#include "guest.h"', '#include <stdio.h>\n#include <stdlib.h>\n#include "guest.h"', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('mode probe in')
