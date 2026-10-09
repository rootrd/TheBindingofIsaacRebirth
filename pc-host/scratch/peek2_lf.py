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
for k in range(nsec):
    b = so + k * 40
    name = d[b:b+8].rstrip(b"\0").decode()
    vsize, va, rsize, raw = struct.unpack_from("<IIII", d, b + 8)
    secs.append((name, va, max(vsize, rsize), raw, rsize))
    print(name, "va", hex(va), "size", hex(max(vsize,rsize)), "raw", hex(raw), "rsize", hex(rsize))
def off(rva):
    for name, va, size, raw, rsize in secs:
        if va <= rva < va + size:
            return raw + (rva - va)
needle = b"checksum is invalid"
i = d.find(needle)
# find which section contains it
for name, va, size, raw, rsize in secs:
    if raw <= i < raw + rsize:
        r = va + (i - raw)
        print("string in", name, "rva", hex(r), "guest", hex(0x30000000 + r))
