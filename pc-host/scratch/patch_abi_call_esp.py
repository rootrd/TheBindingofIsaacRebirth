import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\guest.c'
src = io.open(p, encoding='utf-8', newline='').read()

old = """            uint32_t esi = c->esi;
            uint32_t edi = c->edi;
            uint32_t ebx = c->ebx;
            fn(c);
            if (c->esi != esi || c->edi != edi || c->ebx != ebx) {"""
new = """            uint32_t esi = c->esi;
            uint32_t edi = c->edi;
            uint32_t ebx = c->ebx;
            uint32_t esp = c->esp;
            fn(c);
            if (c->esp != esp) {
                GL_ERR("ABI: callee rva=0x%{public}08x returned with esp 0x%{public}08x "
                       "instead of 0x%{public}08x (drift %{public}d) ret=0x%{public}08x",
                       image_rva(addr), c->esp, esp, (int32_t)(c->esp - esp), ld32(esp));
            }
            if (c->esi != esi || c->edi != edi || c->ebx != ebx) {"""
assert src.count(old) == 1, src.count(old)
io.open(p, 'w', encoding='utf-8', newline='').write(src.replace(old, new, 1))
print("guest_call now also verifies esp balance")
