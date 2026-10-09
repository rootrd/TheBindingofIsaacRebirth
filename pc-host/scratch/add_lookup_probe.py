"""Probe the mount-name pointer and lookup result inside sub_0056c7e0."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0167.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
for ln in lines:
    if '/* 0056c826  call 0x563120 */' in ln:
        out.append('    if (getenv("ISAAC_DBG_LOOKUP")) { static int lk_ = 0; if (lk_ < 8) { lk_++;')
        out.append('        unsigned nm_ = (unsigned)GR(edi);')
        out.append('        fprintf(stderr, "[DBG lookup#%d] nameptr=%08x name=" , lk_, nm_);')
        out.append('        if (nm_ >= 0x30000000U && nm_ < 0xF0000000U) { unsigned q_;')
        out.append('            for (q_ = 0; q_ < 24U; q_++) { unsigned ch_ = ld8(nm_ + q_);')
        out.append('                if (ch_ == 0U) break;')
        out.append('                fprintf(stderr, "%c", (int)ch_); } }')
        out.append('        fprintf(stderr, " (ptrval=%08x)\\n", (unsigned)ld32(nm_)); } }')
    out.append(ln)
    if '/* 0056c82b  mov esi, eax */' in ln:
        out.append('    if (getenv("ISAAC_DBG_LOOKUP")) { static int lr_ = 0; if (lr_ < 8) { lr_++;')
        out.append('        fprintf(stderr, "[DBG lookup-result#%d] found=%08x\\n",')
        out.append('                lr_, (unsigned)GR(eax)); } }')
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('lookup probes added')
