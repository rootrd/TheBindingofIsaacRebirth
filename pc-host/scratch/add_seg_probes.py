"""Depth probes inside the leaking mode-2 segment of sub_0059c270."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0172.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
tag = 0
for ln in lines:
    if 'GUEST_GPR_FLUSH(c); sub_005ec14c(c); GUEST_GPR_RELOAD(c);' in ln:
        tag += 1
        out.append(ln)
        out.append('    { static int s_%d; if (s_%d < 3) { s_%d++;' % (tag, tag, tag))
        out.append('        fprintf(stderr, "[DBG seg%d-after-memcpy-thunk] depth=%%d esp=%%08x ebp=%%08x\\n",' % tag)
        out.append('                (int)(GR(ebp) - GR(esp)), (unsigned)GR(esp), (unsigned)GR(ebp)); } }')
        continue
    if 'GUEST_GPR_FLUSH(c); sub_005b06f0(c); GUEST_GPR_RELOAD(c);' in ln:
        tag += 1
        out.append(ln)
        out.append('    { static int s_%d; if (s_%d < 3) { s_%d++;' % (tag, tag, tag))
        out.append('        fprintf(stderr, "[DBG seg%d-after-isaac-call] depth=%%d esp=%%08x ebp=%%08x\\n",' % tag)
        out.append('                (int)(GR(ebp) - GR(esp)), (unsigned)GR(esp), (unsigned)GR(ebp)); } }')
        continue
    out.append(ln)
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('probes added: %d' % tag)
