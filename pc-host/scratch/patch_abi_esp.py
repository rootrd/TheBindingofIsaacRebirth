import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\guest.c'
src = io.open(p, encoding='utf-8', newline='').read()

old_struct = "typedef struct { uint32_t esi, edi, ebx, ret; } guest_abi_frame;"
new_struct = "typedef struct { uint32_t esi, edi, ebx, ret, esp; } guest_abi_frame;"
assert old_struct in src
src = src.replace(old_struct, new_struct, 1)

old_flush = """        f->ret = ld32(c->esp);   /* the call site pushed its return address */
    }"""
new_flush = """        f->ret = ld32(c->esp);   /* the call site pushed its return address */
        f->esp = c->esp;
    }"""
assert old_flush in src
src = src.replace(old_flush, new_flush, 1)

old_check = """        if (c->esi != f->esi || c->edi != f->edi || c->ebx != f->ebx) {"""
new_check = """        if (c->esp != f->esp) {
            if (g_abi_reports < 60U) {
                g_abi_reports++;
                GL_ERR("ABI: call site 0x%{public}08x returned with esp 0x%{public}08x "
                       "instead of 0x%{public}08x (drift %{public}d)",
                       f->ret, c->esp, f->esp, (int32_t)(c->esp - f->esp));
            }
            return;
        }
        if (c->esi != f->esi || c->edi != f->edi || c->ebx != f->ebx) {"""
assert old_check in src
src = src.replace(old_check, new_check, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(src)
print("ABI checker now also verifies esp balance")
