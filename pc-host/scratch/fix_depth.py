"""Merge the accidental line breaks inside the depth probes' format strings."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0172.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
i = 0
fixed = 0
while i < len(lines):
    ln = lines[i]
    if '(want 164)' in ln and i + 1 < len(lines) and lines[i + 1].strip() == '",':
        out.append(ln + '\\n",')
        i += 2
        fixed += 1
        continue
    out.append(ln)
    i += 1
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('merged %d' % fixed)
