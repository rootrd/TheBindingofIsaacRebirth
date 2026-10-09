"""Merge the accidental line break inside the balance probes' format strings."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0172.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
i = 0
fixed = 0
while i < len(lines):
    ln = lines[i]
    if ln.rstrip().endswith('(want 12)') and i + 1 < len(lines) and lines[i + 1].strip() == '",':
        out.append(ln + '\\n",')
        i += 2
        fixed += 1
        continue
    out.append(ln)
    i += 1
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('merged %d format strings' % fixed)
