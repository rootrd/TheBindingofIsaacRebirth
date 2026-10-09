import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0166.c'
src = io.open(p, encoding='utf-8', newline='').read()

marker = "    /* 00563bc0  mov ecx, eax */\n"
assert src.count(marker) == 1, src.count(marker)
dbg = ('    { static int n_ = 0;\n'
       '      if (n_ < 3 && getenv("ISAAC_DBG_BUFFER")) {\n'
       '        n_++;\n'
       '        const unsigned char *b_ = (const unsigned char *)(isaac_arena_base + GR(ebp) - 0x218);\n'
       '        fprintf(stderr, "[DBG buffer] %02x %02x %02x %02x %02x %02x %02x %02x | %02x %02x %02x %02x %02x %02x %02x %02x\\n",\n'
       '                b_[0], b_[1], b_[2], b_[3], b_[4], b_[5], b_[6], b_[7],\n'
       '                b_[8], b_[9], b_[10], b_[11], b_[12], b_[13], b_[14], b_[15]);\n'
       '      } }\n')
src = src.replace(marker, marker + dbg, 1)
if '#include <stdio.h>' not in src:
    src = src.replace('#include "guest.h"', '#include <stdio.h>\n#include <stdlib.h>\n#include "guest.h"', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(src)
print("instrumented the validator buffer")
