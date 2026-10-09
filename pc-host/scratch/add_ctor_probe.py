"""Trace the singleton ctor: factory result, ctor entry/exit, global value."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0108.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
for ln in lines:
    if 'GUEST_GPR_FLUSH(c); sub_004b3340(c); GUEST_GPR_RELOAD(c);' in ln:
        out.append(ln)
        out.append('    if (getenv("ISAAC_DBG_CTOR")) {')
        out.append('        fprintf(stderr, "[DBG ctor] factory=%08x singleton_before=%08x\\n",')
        out.append('                (unsigned)GR(eax), (unsigned)ld32((uint32_t)0x307fece4U)); }')
        continue
    if 'GUEST_GPR_FLUSH(c); sub_00007700(c); GUEST_GPR_RELOAD(c);' in ln:
        out.append(ln)
        out.append('    if (getenv("ISAAC_DBG_CTOR")) {')
        out.append('        fprintf(stderr, "[DBG ctor] after 0x7700: singleton=%08x eax=%08x\\n",')
        out.append('                (unsigned)ld32((uint32_t)0x307fece4U), (unsigned)GR(eax)); }')
        continue
    out.append(ln)
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('ctor probes added')
