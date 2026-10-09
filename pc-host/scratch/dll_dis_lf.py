#!/usr/bin/env python3
"""Disassemble named exports of the 32-bit system DLLs, to read off exact
semantics and object layouts for the stream/ios helpers we shim.

Usage: python3 dll_dis.py <dll> <name> [count]
"""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

sys.path.insert(0, "/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch")
DLL = sys.argv[1]
WANT = sys.argv[2]
COUNT = int(sys.argv[3]) if len(sys.argv) > 3 else 40

src = open("/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/gen_msvcp_abi_lf.py").read()
src = src.split("# ---- game imports")[0]
ns = {"__name__": "pe"}
exec(src, ns)
PE = ns["PE"]
md = Cs(CS_ARCH_X86, CS_MODE_32)

pe = PE(DLL)
matches = [n for n in pe.exports if WANT in n]
if not matches:
    print(f"no export matching {WANT!r} in {DLL}")
    sys.exit(1)
for name in sorted(matches)[:4]:
    rva = pe.exports[name]
    print(f"\n===== {name}  rva=0x{rva:08x} =====")
    o = pe.off(rva)
    n = 0
    for ins in md.disasm(pe.data[o:o + COUNT * 8], pe.base + rva):
        print(f"  {ins.address - pe.base:#08x}  {ins.bytes.hex():<18} {ins.mnemonic} {ins.op_str}")
        n += 1
        if n >= COUNT:
            break
