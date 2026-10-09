import struct
import sys

path = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe"
if len(sys.argv) > 1:
    path = sys.argv[1]

data = open(path, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
assert data[e_lfanew:e_lfanew + 4] == b"PE\x00\x00"
coff = e_lfanew + 4
num_sections = struct.unpack_from("<H", data, coff + 2)[0]
opt_size = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
magic = struct.unpack_from("<H", data, opt)[0]
assert magic == 0x10B, hex(magic)  # PE32
image_base = struct.unpack_from("<I", data, opt + 28)[0]
sec0 = opt + opt_size
sections = []
for i in range(num_sections):
    off = sec0 + 40 * i
    name = data[off:off + 8].rstrip(b"\x00").decode("latin1")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, vaddr, vsize, raddr, rsize))
    print(f"sec {name:8s} va={vaddr:#010x} vs={vsize:#010x} ra={raddr:#010x} rs={rsize:#010x}")


def raw_to_va(r):
    for name, vaddr, vsize, raddr, rsize in sections:
        if raddr <= r < raddr + rsize:
            return image_base + vaddr + (r - raddr)
    return None


needle = b"kage_mount_points.dat\x00"
pos = 0
while True:
    off = data.find(needle, pos)
    if off < 0:
        break
    print("file-off", hex(off), "-> VA", hex(raw_to_va(off) or 0))
    s = max(0, off - 160)
    e = min(len(data), off + 160)
    print("  ctx:", data[s:e].replace(b"\x00", b"|").decode("latin1"))
    pos = off + 1
