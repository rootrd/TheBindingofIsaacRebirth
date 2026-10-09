import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0167.c'
src = io.open(p, encoding='utf-8', newline='').read()

marker = "    /* 0056c808  xor ebx, ebx */\n"
assert src.count(marker) == 1, src.count(marker)
dbg = ('    if (getenv("ISAAC_DBG_CALL")) fprintf(stderr, '
       '"[DBG56c7e0 prologue-end] esp=0x%08x ebp=0x%08x words: %08x %08x %08x %08x %08x %08x\\n", '
       '(unsigned)c->esp, (unsigned)c->ebp, '
       '(unsigned)ld32(c->esp), (unsigned)ld32(c->esp + 4), (unsigned)ld32(c->esp + 8), '
       '(unsigned)ld32(c->esp + 12), (unsigned)ld32(c->esp + 16), (unsigned)ld32(c->esp + 20));\n')
src = src.replace(marker, dbg + marker, 1)

# also widen the before-pops print with the same stack window
old = ('    if (getenv("ISAAC_DBG_CALL")) fprintf(stderr, "[DBG56c7e0 before-pops] '
       'esi=0x%08x edi=0x%08x ebx=0x%08x esp=0x%08x ebp=0x%08x\\n", '
       '(unsigned)c->esi, (unsigned)c->edi, (unsigned)c->ebx, (unsigned)c->esp, (unsigned)c->ebp);\n')
assert src.count(old) == 1, src.count(old)
new = old.replace('ebp=0x%08x\\n", ', 'ebp=0x%08x\\n", ') 
new = old + ('    if (getenv("ISAAC_DBG_CALL")) fprintf(stderr, '
             '"[DBG56c7e0 before-pops-words] %08x %08x %08x %08x %08x %08x\\n", '
             '(unsigned)ld32(c->esp), (unsigned)ld32(c->esp + 4), (unsigned)ld32(c->esp + 8), '
             '(unsigned)ld32(c->esp + 12), (unsigned)ld32(c->esp + 16), (unsigned)ld32(c->esp + 20));\n')
src = src.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(src)
print("instrumented prologue end")
