#!/usr/bin/env python3
"""Compare a translated function's instruction comments against the native
binary, instruction by instruction.

A single mistranslated instruction (wrong register, wrong offset, missing
instruction) shows up as a mismatch here instead of as a wild pointer hours
later. Operand text is compared after stripping spaces, since the two
disassemblers differ only in spacing.

Usage: python3 verify_func.py <rva-hex> [count]
"""
import glob, re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

GEN = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/cpp/isaac/generated"
EXE = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe"

# ---- native side -----------------------------------------------------------
class PE:
    def __init__(self, path):
        d = self.data = open(path, "rb").read()
        e = struct.unpack_from("<I", d, 0x3C)[0]
        coff = e + 4
        nsec = struct.unpack_from("<H", d, coff + 2)[0]
        optsz = struct.unpack_from("<H", d, coff + 16)[0]
        opt = coff + 20
        self.base = struct.unpack_from("<I", d, opt + 28)[0]
        self.secs = []
        so = opt + optsz
        for i in range(nsec):
            b = so + i * 40
            vsize, va, rsize, raw = struct.unpack_from("<IIII", d, b + 8)
            self.secs.append((va, max(vsize, rsize), raw))

    def off(self, rva):
        for va, size, raw in self.secs:
            if va <= rva < va + size:
                return raw + (rva - va)
        return None

pe = PE(EXE)
md = Cs(CS_ARCH_X86, CS_MODE_32)

COMMENT = re.compile(r"/\*\s*([0-9a-f]{8})\s+([0-9a-f]+)\s+(.*?)\s*\*/")
COMMENT2 = re.compile(r"/\*\s*([0-9a-f]{8})\s+(.*?)\s*\*/")

IMG_BASE = 0x400000          # the PE's own image base in the file
GUEST_BASE = 0x30000000      # where the loader maps it

def canon(text, native):
    """Rewrite hex literals so both sides express the same address: the native
    side uses file VAs, the translation uses relocated guest VAs for data and
    bare RVAs for branch targets."""
    def sub(m):
        h = int(m.group(0), 16)
        if native:
            if IMG_BASE <= h < IMG_BASE + 0x880000:
                return "0x%x" % (h - IMG_BASE)
        else:
            if GUEST_BASE <= h <= GUEST_BASE + 0x880000:
                return "0x%x" % (h - GUEST_BASE)
        return "0x%x" % h
    return re.sub(r"\s+", "", re.sub(r"0x[0-9a-f]+", sub, text.lower()))

def native_insns(rva, count):
    o = pe.off(rva)
    out = []
    for ins in md.disasm(pe.data[o:o + count * 16], pe.base + rva):
        out.append((ins.address - pe.base, ins.mnemonic + " " + ins.op_str))
        if len(out) >= count:
            break
    return out

def find_translation(rva):
    for path in glob.glob(GEN + "/*.c"):
        with open(path, "r", errors="replace") as f:
            for ln, line in enumerate(f, 1):
                m = COMMENT.search(line) or COMMENT2.search(line)
                if m and int(m.group(1), 16) == rva:
                    return path, ln
    return None, None

def translated_insns(path, start, count):
    out = []
    with open(path, "r", errors="replace") as f:
        for i, line in enumerate(f, 1):
            if i < start:
                continue
            m = COMMENT.search(line)
            if m:
                out.append((int(m.group(1), 16), m.group(3)))
            else:
                m = COMMENT2.search(line)
                if not m:
                    continue
                out.append((int(m.group(1), 16), m.group(2)))
            if len(out) >= count:
                break
    return out

def main():
    rva = int(sys.argv[1], 0)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    path, ln = find_translation(rva)
    if path is None:
        print(f"no translation comment found for rva {rva:#x}")
        return 1
    trans = translated_insns(path, ln, count)
    nat = native_insns(rva, len(trans))
    print(f"translation: {path}:{ln}  ({len(trans)} insns)")
    bad = 0
    for i in range(min(len(nat), len(trans))):
        na, nt = nat[i]
        ta, tt = trans[i]
        nt_ops = nt.split(" ", 1)[1] if " " in nt else nt
        if na != ta or (canon(nt, True) != canon(tt, False) and canon(nt_ops, True) != canon(tt, False)):
            print(f"  MISMATCH @ {ta:#x}:\n      native     '{nt}'\n      translated '{tt}'")
            bad += 1
            if bad > 15:
                print("  ... more mismatches suppressed")
                break
    if bad == 0:
        print("OK: all instructions match (address, mnemonic, operands)")
    else:
        print(f"{bad} mismatches in the first {min(len(nat), len(trans))} instructions")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
