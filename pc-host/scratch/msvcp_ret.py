#!/usr/bin/env python3
"""Read the real MSVCP140.dll export table and report each exported function's
final `ret N` (the authoritative stack-cleanup ABI).

Usage: python3 msvcp_ret.py [dll] [name-substring ...]
"""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

DLL = sys.argv[1] if len(sys.argv) > 1 else "/mnt/c/Windows/System32/MSVCP140.dll"
NEEDLES = sys.argv[2:]

data = open(DLL, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec, = struct.unpack_from("<H", data, coff + 2)
optsz, = struct.unpack_from("<H", data, coff + 16)
opt = coff + 20
magic, = struct.unpack_from("<H", data, opt)
assert magic == 0x10B, "not PE32"
image_base, = struct.unpack_from("<I", data, opt + 28)
dd = opt + 96
exp_rva, exp_size = struct.unpack_from("<II", data, dd)
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
            return raw + (rva - va)
    return None

eo = rva2off(exp_rva)
nfunc, nname = struct.unpack_from("<II", data, eo + 20)[0], struct.unpack_from("<I", data, eo + 24)[0]
addr_rva, = struct.unpack_from("<I", data, eo + 28)
name_rva, = struct.unpack_from("<I", data, eo + 32)
ord_rva, = struct.unpack_from("<I", data, eo + 36)

def cstr(rva):
    o = rva2off(rva)
    e = data.index(b"\0", o)
    return data[o:e].decode("latin1")

exports = {}
for i in range(nname):
    nr, = struct.unpack_from("<I", data, rva2off(name_rva) + i * 4)
    idx, = struct.unpack_from("<H", data, rva2off(ord_rva) + i * 2)
    fr, = struct.unpack_from("<I", data, rva2off(addr_rva) + idx * 4)
    exports[cstr(nr)] = fr

md = Cs(CS_ARCH_X86, CS_MODE_32)

def final_ret(frva):
    """Disassemble forward until a ret; report (addr, mnemonic, op)."""
    off = rva2off(frva)
    code = data[off:off + 4096]
    last = None
    for ins in md.disasm(code, image_base + frva):
        last = (ins.address - image_base, ins.mnemonic, ins.op_str)
        if ins.mnemonic == "ret":
            return last
        if ins.mnemonic.startswith("jmp") and ins.op_str.startswith("0x"):
            tgt = int(ins.op_str, 16) - image_base     # tail call: follow once
            if tgt != last[0]:
                return ("->", f"tailjmp 0x{tgt:08x}", "")
    return last

hits = 0
for name, rva in sorted(exports.items()):
    if NEEDLES and not any(n in name for n in NEEDLES):
        continue
    r = final_ret(rva)
    print(f"{name[4:] if name.startswith('??@') else name}\n    rva=0x{rva:08x} -> {r}")
    hits += 1
    if hits > 40:
        print("... (truncated)")
        break
print(f"[{len(exports)} exports, {hits} matched]")
