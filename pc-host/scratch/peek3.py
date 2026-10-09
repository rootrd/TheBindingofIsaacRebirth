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
    vsize, va, rsize, raw = struct.unpack_from("<IIII", d, b + 8)
    secs.append((va, max(vsize, rsize), raw, rsize))
i = d.find(b"checksum is invalid")
start = d.rfind(b"\0", 0, i) + 1
s = d[start:d.index(b"\0", i)]
def rva(o):
    for va, size, raw, rsize in secs:
        if raw <= o < raw + rsize:
            return va + (o - raw)
r = rva(start)
print("format string:", repr(s))
print("rva", hex(r), "guest", hex(0x30000000 + r))
