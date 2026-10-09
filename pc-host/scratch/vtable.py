import struct
EXE = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe"
d = open(EXE, "rb").read()
e = struct.unpack_from("<I", d, 0x3C)[0]; coff = e + 4
nsec = struct.unpack_from("<H", d, coff + 2)[0]
optsz = struct.unpack_from("<H", d, coff + 16)[0]; opt = coff + 20
secs = []
so = opt + optsz
for k in range(nsec):
    b = so + k * 40
    vsize, va, rsize, raw = struct.unpack_from("<IIII", d, b + 8)
    secs.append((va, max(vsize, rsize), raw))
def off(rva):
    for va, size, raw in secs:
        if va <= rva < va + size:
            return raw + (rva - va)
def rd(guest):
    r = guest - 0x30000000
    o = off(r)
    return struct.unpack_from("<I", d, o)[0]
base = 0x30764894
# find where the file VA lives: this is .data, and relocations apply. The values
# in the file are the unrelocated VAs (base 0x400000).
r = base - 0x30000000
o = off(r)
for i in range(12):
    v = struct.unpack_from("<I", d, o + i*4)[0]
    print(f"vtbl[{i}] (slot +{i*4:#04x}) = {v:#010x} -> rva {v-0x400000:#x}")
