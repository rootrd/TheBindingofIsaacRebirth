import io

# 1) guest_0172.c: strip mode + vread probes
p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0172.c'
s = io.open(p, encoding='utf-8', newline='').read()
lines = s.split('\n')
out, skip = [], 0
for i, ln in enumerate(lines):
    if skip:
        skip -= 1
        continue
    if 'DBG mode' in ln or 'DBG vread' in ln:
        # drop this line and continuation lines until the closing " } }\n"
        while i + skip < len(lines) and '} }\n' not in lines[i + skip] and '} }' not in lines[i + skip]:
            skip += 1
        skip += 1
        continue
    out.append(ln)
s = '\n'.join(out)
if 'fprintf' not in s and '#include <stdio.h>' in s:
    s = s.replace('#include <stdio.h>\n#include <stdlib.h>\n#include "guest.h"', '#include "guest.h"')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('guest_0172 cleaned; fprintf:', s.count('fprintf'))

# 2) guest_0166.c: strip DBG buffer block
q = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0166.c'
t = io.open(q, encoding='utf-8', newline='').read()
i = t.find('    { static int n_ = 0;')
while i != -1:
    j = t.index('      } }\n', i) + len('      } }\n')
    t = t[:i] + t[j:]
    i = t.find('    { static int n_ = 0;')
if 'fprintf' not in t and '#include <stdio.h>' in t:
    t = t.replace('#include <stdio.h>\n#include <stdlib.h>\n#include "guest.h"', '#include "guest.h"')
io.open(q, 'w', encoding='utf-8', newline='').write(t)
print('guest_0166 cleaned; fprintf:', t.count('fprintf'))
