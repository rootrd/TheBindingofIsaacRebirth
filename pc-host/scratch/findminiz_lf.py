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
    secs.append((va, max(vsize, rsize), raw, rsize))
def rva(o):
    for va, size, raw, rsize in secs:
        if raw <= o < raw + rsize:
            return va + (o - raw)
i = d.find(b"MiniZ compressed buffer overflow")
start = d.rfind(b"\0", 0, i) + 1
s = d[start:d.index(b"\0", i)]
r = rva(start)
print("string:", s, "rva:", hex(r) if r else None, "guest:", hex(0x30000000 + r) if r else None)
# find refs
target = r
pat = struct.pack("<I", target)
j = 0
while True:
    j = d.find(pat, j)
    if j < 0: break
    print("ref at file", hex(j), "rva", hex(rva(j)) if rva(j) else None)
    j += 1
