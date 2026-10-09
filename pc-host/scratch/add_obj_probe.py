"""Dump the assigned texture-ref object fully at the 0x3a93a9 use site."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0108.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
for ln in lines:
    if '/* 003a93a9  mov ecx, dword ptr [0xbfece4] */' in ln:
        out.append('    if (getenv("ISAAC_DBG_OBJ")) { static int ob_ = 0; if (ob_ < 4) { ob_++;')
        out.append('        unsigned g_ = (unsigned)ld32((uint32_t)0x307fece4U), k_;')
        out.append('        fprintf(stderr, "[DBG obj#%d] global=%08x bytes=", ob_, g_);')
        out.append('        if (g_ >= 0x1000U && g_ < 0xF0000000U) {')
        out.append('            for (k_ = 0; k_ < 24U; k_++) fprintf(stderr, "%02x", ld8(g_ + k_));')
        out.append('            fprintf(stderr, " f0=%08x f4=%08x", (unsigned)ld32(g_), (unsigned)ld32(g_ + 4U));')
        out.append('        } else { fprintf(stderr, "unmapped"); }')
        out.append('        fprintf(stderr, " outbuf=%08x:", (unsigned)(GR(ebp) - 0x28U)); { unsigned k2_;')
        out.append('            for (k2_ = 0; k2_ < 24U; k2_++) fprintf(stderr, "%02x",')
        out.append('                ld8((unsigned)(GR(ebp) - 0x28U) + k2_)); }')
        out.append('        fprintf(stderr, "\\n"); } }')
    out.append(ln)
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('obj probe added')
