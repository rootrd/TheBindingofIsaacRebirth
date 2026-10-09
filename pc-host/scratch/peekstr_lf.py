import struct
EXE = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe"
d = open(EXE, "rb").read()
e = struct.unpack_from("<I", d, 0x3C)[0]
coff = e + 4
nsec = struct.unpack_from("<H", d, coff + 2)[0]
optsz = struct.unpack_from("<H", d, coff + 16)[0]
opt = coff + 20
secs = []
so = opt + optsz
for i in range(nsec):
    b = so + i * 40
    vsize, va, rsize, raw = struct.unpack_from("<IIII", d, b + 8)
    secs.append((va, max(vsize, rsize), raw))
def off(rva):
    for va, size, raw in secs:
        if va <= rva < va + size:
            return raw + (rva - va)
    return None
for rva in (0x74d734, 0x7bf948, 0x7aa758, 0x73ee54):
    o = off(rva)
    if o is None:
        print(hex(rva), "unmapped"); continue
    s = d[o:o+64].split(b"\0")[0]
    print(hex(rva), repr(s[:64]))
