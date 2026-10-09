"""Probe the mods-singleton gate call at 0x3a938a."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0108.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
for ln in lines:
    if '/* 003a938a  call eax */' in ln:
        out.append('    if (getenv("ISAAC_DBG_GATE")) { static int gp_ = 0; if (gp_ < 6) { gp_++;')
        out.append('        fprintf(stderr, "[DBG gate#%d] obj=%08x vtbl=%08x target=%08x\\n",')
        out.append('                gp_, (unsigned)GR(ecx),')
        out.append('                (unsigned)ld32((unsigned)GR(ecx)), (unsigned)GR(eax)); } }')
    out.append(ln)
    if 'GUEST_GPR_FLUSH(c); guest_call(c, _target); GUEST_GPR_RELOAD(c);' in ln:
        out.append('    if (getenv("ISAAC_DBG_GATE")) { static int gq_ = 0; if (gq_ < 6) { gq_++;')
        out.append('        fprintf(stderr, "[DBG gate-result#%d] eax=%08x al=%u\\n",')
        out.append('                gq_, (unsigned)GR(eax), (unsigned)(GR(eax) & 0xFFU)); } }')
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('gate probes added')
