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
    secs.append((va, max(vsize, rsize), raw))
def off(rva):
    for va, size, raw in secs:
        if va <= rva < va + size:
            return raw + (rva - va)
def rva(o):
    for va, size, raw in secs:
        if raw <= o < raw + size:
            return va + (o - raw)
target = 0x75d69a
pat = b"\x68" + struct.pack("<I", target)          # push imm32
pat2 = struct.pack("<I", target)                    # any absolute reference
print("push refs:")
start = 0
while True:
    i = d.find(pat, start)
    if i < 0: break
    print("   code rva", hex(rva(i)))
    start = i + 1
print("all absolute refs (first 10):")
start = 0
n = 0
while n < 10:
    i = d.find(pat2, start)
    if i < 0: break
    print("   rva", hex(rva(i)))
    start = i + 1
    n += 1
