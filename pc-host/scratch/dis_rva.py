#!/usr/bin/env python3
"""Disassemble native code at guest RVAs from isaac-ng.exe (Steamless-rewritten PE).

Usage: python3 dis_rva.py 0x2597b0 [count]
"""
import sys, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe"

data = open(EXE, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
assert data[e_lfanew:e_lfanew+4] == b"PE\0\0", "not PE"
coff = e_lfanew + 4
nsec, = struct.unpack_from("<H", data, coff + 2)
optsz, = struct.unpack_from("<H", data, coff + 16)
opt = coff + 20
magic, = struct.unpack_from("<H", data, opt)
image_base, = struct.unpack_from("<I", data, opt + 28)
secs = []
so = opt + optsz
for i in range(nsec):
    b = so + i * 40
    name = data[b:b+8].rstrip(b"\0").decode("latin1")
    vsize, va, rsize, raw = struct.unpack_from("<IIII", data, b + 8)
    secs.append((name, va, vsize, raw, rsize))

def rva2off(rva):
    for name, va, vsize, raw, rsize in secs:
        if va <= rva < va + max(vsize, rsize):
            return raw + (rva - va), name
    return None, None

md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = False

def dis(rva, count):
    off, sec = rva2off(rva)
    if off is None:
        print(f"RVA {rva:#x}: not mapped")
        return
    print(f"=== RVA {rva:#08x} (VA {image_base+rva:#010x}, sec {sec}, off {off:#x}) ===")
    code = data[off:off + count * 8]
    n = 0
    for ins in md.disasm(code, image_base + rva):
        print(f"{ins.address - image_base:#08x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")
        n += 1
        if n >= count:
            break

if __name__ == "__main__":
    rva = int(sys.argv[1], 0)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    print(f"image_base={image_base:#x} sections={[(s[0],hex(s[1])) for s in secs]}")
    dis(rva, count)
