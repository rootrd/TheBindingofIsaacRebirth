"""Make the pop-delta probe key on the ebp-relative frame depth (frame-independent)."""
import io

P = 'isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0172.c'
lines = io.open(P, encoding='utf-8').read().split('\n')
out = []
i = 0
while i < len(lines):
    ln = lines[i]
    if 'DBG pop-delta' in ln:
        # replace the whole probe block (from the 'if (g_pre_pop_esp' line to the closing)
        # find the start: previous line holds 'g_pre_pop_esp = ...'
        j = len(out) - 1
        while j >= 0 and 'g_pre_pop_esp = ' not in out[j]:
            j -= 1
        del out[j:]
        out.append('    g_pre_pop_esp = (uint32_t)GR(esp);')
        out.append('    if ((uint32_t)(GR(ebp) - g_pre_pop_esp) != 164U) {')
        out.append('        static int pd_ = 0;')
        out.append('        if (pd_ < 3) { pd_++;')
        out.append('            unsigned k_;')
        out.append('            fprintf(stderr, "[DBG pop-delta#%d] ebp=%08x esp=%08x depth=%d (want 164) stk=",')
        out.append('                    pd_, (unsigned)GR(ebp), (unsigned)GR(esp),')
        out.append('                    (int)(GR(ebp) - GR(esp)));')
        out.append('            for (k_ = 0; k_ < 8U; k_++)')
        out.append('                fprintf(stderr, "%08x ",')
        out.append('                        (unsigned)ld32((unsigned)GR(esp) + k_ * 4U));')
        out.append('            fprintf(stderr, "\\n"); } }')
        # skip to the end of the old block: the line with 'GR(ebx) = (uint32_t)(GPOP());'
        while i < len(lines) and 'GR(ebx) = (uint32_t)(GPOP());' not in lines[i]:
            i += 1
        continue
    out.append(ln)
    i += 1
io.open(P, 'w', encoding='utf-8').write('\n'.join(out))
print('pop-delta probe rewritten')
