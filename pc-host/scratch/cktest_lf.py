import struct, zlib
M = 0xFFFFFFFF
def gamehash(data):
    s = 0
    pad = data + b"\0" * ((4 - len(data) % 4) % 4)
    for w in struct.unpack("<%dI" % (len(pad)//4), pad):
        s = (((s << 31) & M) + w + (s >> 1)) & M
    return s
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", d, 8)
ok = 0; bad = 0
for e in range(min(entries, 40)):
    a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off + e*20)
    csize = struct.unpack_from("<I", d, off)[0]
    blob = d[off+4:off+4+csize]
    try:
        out = zlib.decompressobj(-15).decompress(blob)
    except Exception as ex:
        print(f"e{e}: inflate fail {ex}"); bad += 1; continue
    if len(out) != size:
        print(f"e{e}: size mismatch got={len(out)} want={size}"); bad += 1; continue
    h = gamehash(out)
    status = "OK " if h == ck else "BAD"
    if h == ck: ok += 1
    else: bad += 1
    if e < 6: print(f"e{e}: {status} hash={h:08x} want={ck:08x} head={out[:20]!r}")
print(f"of 40: ok={ok} bad={bad}")
