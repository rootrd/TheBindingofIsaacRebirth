"""Repair the balance-probe block: unify the static names and put the escapes back."""
import io
import re

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0172.c'
s = io.open(P, encoding='utf-8').read()

# 1) unify names: g_bal_c2cb -> g_bal_0059c2cb etc.
for tag in ['c2cb', 'c2fd', 'c337', 'c380', 'c44a']:
    s = s.replace('g_bal_%s' % tag, 'g_bal_0059%s' % tag)
    s = s.replace('b_%s' % tag, 'b_0059%s' % tag)

# 2) repair the broken fprintf format strings (a stray real newline inside "...")
def fix(match):
    return match.group(1) + '\\n",\n'

s = re.sub(r'(\[DBG bal-0059[0-9a-f]{3}\] esp before=%08x after=%08x delta=%d \(want 12\))\n",\n',
           r'\1\\n",\n', s)

io.open(P, 'w', encoding='utf-8').write(s)
print('repaired')
