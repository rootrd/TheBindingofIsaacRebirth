"""Pair-probe the mods gate: record the call target at 0x3a938a and its result."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0108.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
for i, ln in enumerate(lines):
    out.append(ln)
    if '/* 003a938c  test al, al */' in ln:
        out.append('    if (getenv("ISAAC_DBG_GATE")) {')
        out.append('        fprintf(stderr, "[DBG gate-call] eax=%08x al=%u\\n",')
        out.append('                (unsigned)GR(eax), (unsigned)(GR(eax) & 0xFFU)); }')
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('paired probe added')
