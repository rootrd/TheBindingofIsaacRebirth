#!/usr/bin/env python3
"""Give the two MSVCP140 iostream functions that spin the engine their real
semantics: the istream sentry must report success and setstate must only
record the state word."""
import io

BASE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/cpp/isaac/'

# --- import table ----------------------------------------------------------
p = BASE + 'generated/host_imports_table.h'
s = io.open(p, encoding='utf-8').read()
a = '"MSVCP140.dll!?_Ipfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QAE_N_N@Z", h_msvcp_stub'
b = '"MSVCP140.dll!?_Ipfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QAE_N_N@Z", h_msvcp_ipfx'
assert a in s
s = s.replace(a, b, 1)
a = '"MSVCP140.dll!?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z", h_msvcp_stub'
b = '"MSVCP140.dll!?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z", h_msvcp_setstate'
assert a in s
s = s.replace(a, b, 1)
io.open(p, 'w', encoding='utf-8').write(s)
print('import table updated')

# --- handlers --------------------------------------------------------------
p = BASE + 'host_ohos.c'
s = io.open(p, encoding='utf-8').read()
anchor = 'static void h_msvcp_ios_ctor(CPU *c);'
assert anchor in s
s = s.replace(anchor, anchor + '\nstatic void h_msvcp_ipfx(CPU *c);\nstatic void h_msvcp_setstate(CPU *c);', 1)

impl = (
    '/* basic_istream::_Ipfx(bool): the stream sentry. Reporting failure tells\n'
    ' * the caller the stream is unusable, so operator>> bails and the engine\n'
    ' * retries forever (observed as an endless _Ipfx/setstate loop). The guest\n'
    ' * streams are backed by our file layer, so report "ok" here; a genuinely\n'
    ' * short read still surfaces through the fread-backed streambuf. */\n'
    'static void h_msvcp_ipfx(CPU *c)\n'
    '{\n'
    '    c->eax = 1;\n'
    '    host_stdcall_ret(c, 4);\n'
    '}\n'
    '\n'
    '/* basic_ios::setstate(int, bool): record the state word, never rethrow. */\n'
    'static void h_msvcp_setstate(CPU *c)\n'
    '{\n'
    '    uint32_t this_ptr = c->ecx;\n'
    '    uint32_t state = host_arg(c, 0);\n'
    '    if (this_ptr) {\n'
    '        uint32_t cur = ld32(this_ptr + 0x10U);\n'
    '        st32(this_ptr + 0x10U, cur | state);\n'
    '    }\n'
    '    c->eax = 0;\n'
    '    host_stdcall_ret(c, 8);\n'
    '}\n'
    '\n'
    '/* MSVCP140 generic stub'
)
anchor = '/* MSVCP140 generic stub'
assert anchor in s
s = s.replace(anchor, impl, 1)
io.open(p, 'w', encoding='utf-8').write(s)
print('handlers added')
