import struct
EXE = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe"
d = open(EXE, "rb").read()
needle = b"checksum is invalid"
i = d.find(needle)
print("file off:", hex(i) if i >= 0 else None)
e = struct.unpack_from("<I", d, 0x3C)[0]
coff = e + 4
nsec = struct.unpack_from("<H", d, coff + 2)[0]
optsz = struct.unpack_from("<H", d, coff + 16)[0]
opt = coff + 20
secs = []
so = opt + optsz
for k in range(nsec):
    b = so + k * 40
    vsize, va, rsize, raw = struct.unpack_from("<IIII", d, b + 8)
    secs.append((va, max(vsize, rsize), raw))
def rva(o):
    for va, size, raw in secs:
        if raw <= o < raw + size:
            return va + (o - raw)
    return None
r = rva(i)
print("rva:", hex(r) if r else None, "guest va:", hex(0x30000000 + r) if r else None)
for n2 in (b"from resources/packed", b"load archives"):
    j = d.find(n2)
    print(n2, "-> rva", hex(rva(j)) if j >= 0 else None)
