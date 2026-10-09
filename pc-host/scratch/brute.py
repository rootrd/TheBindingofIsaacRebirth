import struct, zlib
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/animations.a"
d = open(P, "rb").read()
M = 0xFFFFFFFF
def gamehash(out):
    s = 0
    pad = out + b"\0" * ((4 - len(out) % 4) % 4)
    for w in struct.unpack("<%dI" % (len(pad)//4), pad):
        s = (((s << 31) & M) + w + (s >> 1)) & M
    return s
TARGET = 0x50325a6a
USIZE = 4049421
hits = 0
for B in range(0, 64):
    p = B + 14
    if p + 4 > len(d): break
    csize = struct.unpack_from("<I", d, p)[0]
    for cstart in (p, p + 4):
        for wbits, tag in ((-15, "raw"), (15, "zlib")):
            if csize == 0 or csize > 1000000 or cstart + csize > len(d): continue
            try:
                out = zlib.decompressobj(wbits).decompress(d[cstart:cstart+csize])
            except Exception:
                continue
            h = gamehash(out)
            print(f"B={B} csize={csize} {tag}: {len(out)} bytes hash={h:08x} {'*** MATCH ***' if h == TARGET and len(out) == USIZE else ''}")
            hits += 1
print("inflate successes:", hits)
