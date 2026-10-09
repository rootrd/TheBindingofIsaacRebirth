"""Dump what the singleton factory actually produced."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0108.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
for ln in lines:
    out.append(ln)
    if 'GUEST_GPR_FLUSH(c); sub_004b3340(c); GUEST_GPR_RELOAD(c);' in ln:
        out.append('    if (getenv("ISAAC_DBG_FAC")) {')
        out.append('        fprintf(stderr, "[DBG fac] out=%08x f0=%08x f4=%08x f8=%08x fc=%08x\\n",')
        out.append('                (unsigned)GR(eax),')
        out.append('                (unsigned)ld32((unsigned)GR(eax)),')
        out.append('                (unsigned)ld32((unsigned)GR(eax) + 4U),')
        out.append('                (unsigned)ld32((unsigned)GR(eax) + 8U),')
        out.append('                (unsigned)ld32((unsigned)GR(eax) + 12U)); }')
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('factory probe added')
