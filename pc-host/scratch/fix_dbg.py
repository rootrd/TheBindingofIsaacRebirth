import io, re

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0144.c'
src = io.open(p, encoding='utf-8', newline='').read()

start = src.index('    if (getenv("ISAAC_DBG_CALL"))')
end = src.index('    /* 004b3418  mov edx', start)
dbg = ('    if (getenv("ISAAC_DBG_CALL")) fprintf(stderr, '
       '"[DBG] after 56c7e0 esi=0x%08x esp=0x%08x ebp=0x%08x arg=0x%08x\\n", '
       '(unsigned)c->esi, (unsigned)c->esp, (unsigned)c->ebp, '
       '(unsigned)ld32(c->ebp + 8U));\n')
src = src[:start] + dbg + src[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(src)
print("fixed debug line")
