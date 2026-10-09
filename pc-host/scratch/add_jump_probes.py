"""Add depth probes at every `goto L_0059c671` inside sub_0059c270."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0172.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
n = 0
for ln in lines:
    if ln.strip() == 'goto L_0059c671;':
        n += 1
        out.append('    { static int j_%d; if (j_%d < 3) { j_%d++;' % (n, n, n))
        out.append('        fprintf(stderr, "[DBG jump671-%d] ebp=%%08x esp=%%08x depth=%%d (want 164) ebx=%%08x\\n",' % n)
        out.append('                (unsigned)GR(ebp), (unsigned)GR(esp),')
        out.append('                (int)(GR(ebp) - GR(esp)), (unsigned)GR(ebx)); } }')
    out.append(ln)
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('probes at %d jump sites' % n)
