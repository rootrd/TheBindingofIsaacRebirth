"""Trace esp through sub_005b06f0 (the ISAAC block decrypt)."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0175.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
state = 0
for ln in lines:
    if ln.startswith('void sub_005b06f0(CPU *__restrict c)'):
        state = 1
        out.append(ln)
        out.append('{')
        out.append('    { static int k_; if (k_ < 4) { k_++;')
        out.append('        fprintf(stderr, "[DBG isaac-enter#%d] ret=%%08x esp=%%08x\\n",')
        out.append('                (unsigned)ld32((unsigned)GR(esp)), (unsigned)GR(esp)); } }')
        continue
    if state == 1 and ln.strip() == '{':
        continue
    if state == 1 and '/* 005b0751  ret 8 */' in ln:
        out.append('    { static int k2_; if (k2_ < 4) { k2_++;')
        out.append('        fprintf(stderr, "[DBG isaac-exit#%d] esp=%%08x ebp=%%08x\\n",')
        out.append('                (unsigned)GR(esp), (unsigned)GR(ebp)); } }')
        state = 2
    out.append(ln)
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('isaac probes added (state=%d)' % state)
