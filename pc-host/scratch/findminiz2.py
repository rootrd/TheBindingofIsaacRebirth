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
target = 0x7650bc
# the compiler may reference guest VA 0x307650bc after relocation OR the file-VA 0xb650bc
for t in (0x307650bc, 0x400000 + target, target):
    pat = struct.pack("<I", t)
    j = 0
    while True:
        j = d.find(pat, j)
        if j < 0: break
        print(hex(t), "ref at rva", hex(rva(j)) if rva(j) else hex(j))
        j += 1
