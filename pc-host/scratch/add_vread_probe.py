import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0172.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = ('    /* 0059c380  call dword ptr [eax + 0x14] */' + nl)
assert s.count(anchor) == 1, s.count(anchor)
probe = ('    if (getenv("ISAAC_DBG_BUFFER")) { static int vn_ = 0; if (vn_ < 8) { vn_++;' + nl +
         '        fprintf(stderr, "[DBG vread] ecx=%08x vtbl=%08x method=%08x edx=%08x ebx=%08x\\n",' + nl +
         '                (unsigned)GR(ecx), (unsigned)GR(eax),' + nl +
         '                (unsigned)ld32(GR(eax) + 0x14U), (unsigned)GR(edx), (unsigned)GR(ebx)); } }' + nl)
s = s.replace(anchor, probe + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('vread probe installed')
