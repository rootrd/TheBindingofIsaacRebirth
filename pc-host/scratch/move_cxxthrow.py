#!/usr/bin/env python3
"""h_cxx_throw needs isaac_seh_dispatch; move its definition below the
dispatcher instead of declaring things out of order."""
import io

p = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/cpp/isaac/host_ohos.c'
s = io.open(p, encoding='utf-8').read()

start = s.index('/* MSVC C++ throw.')
end = s.index('static void h_RaiseException(CPU *c)\n{')
block = s[start:end]
s = s[:start] + s[end:]

# re-insert right before the RaiseException handler that the dispatcher serves
anchor = 'static void h_RaiseException(CPU *c)\n{\n    uint32_t code = host_arg(c, 0);'
assert anchor in s
s = s.replace(anchor, block + anchor, 1)

# the forward declaration is now unnecessary
s = s.replace('static void h_RaiseException(CPU *c);\nstatic void h_cxx_throw(CPU *c);',
              'static void h_RaiseException(CPU *c);\nstatic void h_cxx_throw(CPU *c);', 1)

io.open(p, 'w', encoding='utf-8').write(s)
print('moved')
