import struct, zlib
M = 0xFFFFFFFF
def fold(data, init=0xababeb98):
    s = init
    pad = data + b"\0" * ((4 - len(data) % 4) % 4)
    for w in struct.unpack("<%dI" % (len(pad)//4), pad):
        s = (((s << 31) & M) + ((w + (s >> 1)) & M)) & M
    return s
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", d, 8)
ok = 0
for e in range(min(entries, 50)):
    a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off + e*20)
    pos = off
    dobj = zlib.decompressobj(-15)
    total = b""
    try:
        while len(total) < size and pos + 4 <= idx_off:
            v = struct.unpack_from("<I", d, pos)[0]
            ln = v & 0x7fffffff
            pos += 4
            total += dobj.decompress(d[pos:pos+ln])
            pos += ln
            if v & 0x80000000:
                break
    except Exception:
        print(f"e{e}: inflate error at chunk {len(lens) if False else ''}"); continue
    if len(total) != size:
        print(f"e{e}: size {len(total)} != {size}")
        continue
    h = fold(total)
    if h == ck: ok += 1
    else: print(f"e{e}: MISMATCH {h:08x} vs {ck:08x}")
print(f"{ok}/{min(entries,50)} checksums MATCH (init=0xababeb98 over plaintext)")
