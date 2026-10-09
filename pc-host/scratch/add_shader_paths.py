#!/usr/bin/env python3
"""The engine asks for "shaders/x.vs" relative to the data root, but the
resources live under "resources/shaders/". Mirror the shader block in the
unpack list under the bare "shaders/" prefix the guest actually opens."""
import io

PAGE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/ets/pages/Index.ets'

src = io.open(PAGE, encoding='utf-8').read()
first = "    'resources/shaders/bg.vs',"
last = "    'resources/shaders/water_v2.fs'"
start = src.index(first)
end = src.index(last) + len(last)
block = src[start:end]
mirrored = block.replace("    'resources/shaders/", "    'shaders/")
src = src[:end + 1] + mirrored + ',\n' + src[end + 1:]
io.open(PAGE, 'w', encoding='utf-8').write(src)
print('mirrored shader paths under shaders/')
