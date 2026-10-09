#!/usr/bin/env python3
"""Route _CxxThrowException through the SEH dispatcher instead of letting it
return into the int3 that follows every throw site."""
import io

BASE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/cpp/isaac/'

p = BASE + 'generated/host_imports_table.h'
s = io.open(p, encoding='utf-8').read()
a = '"VCRUNTIME140.dll!_CxxThrowException", h_vcruntime_stub'
b = '"VCRUNTIME140.dll!_CxxThrowException", h_cxx_throw'
assert a in s
s = s.replace(a, b, 1)
io.open(p, 'w', encoding='utf-8').write(s)
print('table updated')

p = BASE + 'host_ohos.c'
s = io.open(p, encoding='utf-8').read()

anchor = 'static void h_RaiseException(CPU *c);'
assert anchor in s
s = s.replace(anchor, anchor + '\nstatic void h_cxx_throw(CPU *c);', 1)

impl = (
    '/* MSVC C++ throw. The translated throw sites call this and then fall\n'
    ' * into an int3 (the compiler assumes the call never returns), so simply\n'
    ' * returning zeros faults at the int3 instead of unwinding. Hand the\n'
    ' * exception to the SEH dispatcher, which walks the guest FS:[0] chain and\n'
    ' * offers it to each frame handler. */\n'
    'static void h_cxx_throw(CPU *c)\n'
    '{\n'
    '    uint32_t obj = host_arg(c, 0);\n'
    '    uint32_t ti = host_arg(c, 1);\n'
    '    HOST_ERR("CxxThrowException: obj=0x%{public}08x ti=0x%{public}08x",\n'
    '             obj, ti);\n'
    '    isaac_seh_dispatch(c, MSVC_CXX_EXCEPTION_CODE, obj, ti);\n'
    '    /* The dispatcher faults when nothing claims the exception. */\n'
    '}\n'
    '\n'
    'static void h_RaiseException(CPU *c)'
)
s = s.replace(anchor, impl, 1)
io.open(p, 'w', encoding='utf-8').write(s)
print('handler added')
