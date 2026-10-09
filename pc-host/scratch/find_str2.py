import struct, sys
data = open("/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe","rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
num_sections = struct.unpack_from("<H", data, coff + 2)[0]
opt_size = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
image_base = struct.unpack_from("<I", data, opt + 28)[0]
sec0 = opt + opt_size
sections = []
for i in range(num_sections):
    off = sec0 + 40 * i
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((vaddr, vsize, raddr, rsize))
def raw_to_va(r):
    for vaddr, vsize, raddr, rsize in sections:
        if raddr <= r < raddr + rsize:
            return image_base + vaddr + (r - raddr)
    return 0
needle = b"resources-dlc3/\x00"
pos = 0
while True:
    off = data.find(needle, pos)
    if off < 0: break
    print("VA", hex(raw_to_va(off)), "guest", hex(0x30000000 + raw_to_va(off) - image_base))
    s = max(0, off-120); e = min(len(data), off+120)
    print("ctx:", data[s:e].replace(b"\x00", b"|").decode("latin1"))
    pos = off + 1
