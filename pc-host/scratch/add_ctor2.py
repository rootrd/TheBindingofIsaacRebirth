"""Probe inside sub_00007700: the source pointer and the built temp."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0000.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
for ln in lines:
    if 'GUEST_GPR_FLUSH(c); sub_000078a0(c); GUEST_GPR_RELOAD(c);' in ln and 'call 0x4078a0' in out[-1]:
        out.append(ln)
        out.append('    if (getenv("ISAAC_DBG_CTOR2")) {')
        out.append('        fprintf(stderr, "[DBG ctor2] src=%08x temp=%08x temp0=%08x src0=%08x\\n",')
        out.append('                (unsigned)ld32((unsigned)(GR(ebp) + 8U)),')
        out.append('                (unsigned)(GR(ebp) - 0x18U),')
        out.append('                (unsigned)ld32((unsigned)(GR(ebp) - 0x18U)),')
        out.append('                (unsigned)ld32((unsigned)(GR(ebp) + 8U))); }')
        continue
    out.append(ln)
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('done')
