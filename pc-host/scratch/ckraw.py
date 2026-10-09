import struct
M = 0xFFFFFFFF
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", d, 8)
def fold(data, init=0):
    s = init
    pad = data + b"\0" * ((4 - len(data) % 4) % 4)
    for w in struct.unpack("<%dI" % (len(pad)//4), pad):
        s = (((s << 31) & M) + ((w + (s >> 1)) & M)) & M
    return s
for e in range(6):
    a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off + e*20)
    # walk chunks from off until the top-bit-flag chunk, INCLUDING prefixes
    pos = off
    raw = b""
    while True:
        v = struct.unpack_from("<I", d, pos)[0]
        ln = v & 0x7fffffff
        raw += d[pos:pos+4+ln]          # prefix + data
        pos += 4 + ln
        if v & 0x80000000:
            break
    h = fold(raw)
    print(f"e{e}: raw={len(raw)} fold={h:08x} want={ck:08x} {'*** MATCH ***' if h == ck else ''}")
