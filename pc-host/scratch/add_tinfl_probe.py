import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0172.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = '    /* 0059c3f3  call 0x5aeb00 */' + nl
assert s.count(anchor) == 1, s.count(anchor)
probe = ('    if (getenv("ISAAC_DBG_BUFFER")) { static int tn_ = 0; if (tn_ < 6) { tn_++;' + nl +
         '        unsigned dec2_ = (unsigned)ld32((unsigned)(GR(ebp) + (int)(-140)));' + nl +
         '        fprintf(stderr, "[DBG tinfl] dec=%08x tinflstate=%08x insize=%08x outsize=%08x in=%02x%02x%02x%02x%02x%02x%02x%02x\\n",' + nl +
         '                (unsigned)GR(edx), (unsigned)GR(ecx),' + nl +
         '                (unsigned)ld32((unsigned)(GR(ebp) + (int)(-144))),' + nl +
         '                (unsigned)ld32((unsigned)(GR(ebp) + (int)(-152))),' + nl +
         '                ld8((unsigned)(GR(edi) + 0x1cU)), ld8((unsigned)(GR(edi) + 0x1dU)),' + nl +
         '                ld8((unsigned)(GR(edi) + 0x1eU)), ld8((unsigned)(GR(edi) + 0x1fU)),' + nl +
         '                ld8((unsigned)(GR(edi) + 0x20U)), ld8((unsigned)(GR(edi) + 0x21U)),' + nl +
         '                ld8((unsigned)(GR(edi) + 0x22U)), ld8((unsigned)(GR(edi) + 0x23U))); } }' + nl)
s = s.replace(anchor, probe + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('tinfl probe in')
