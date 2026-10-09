import struct
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/animations.a"
d = open(P, "rb").read()
mode = d[7]
idx_off, entries = struct.unpack_from("<IH", d, 8)
print(f"mode={mode} idx_off={idx_off} entries={entries}")
M = 0xFFFFFFFF
def h(words, init):
    s = init & M
    for w in words:
        c = (s >> 1) & M
        s = ((s << 31) & M)
        s = (s + w + c) & M
    return s
for i in range(entries):
    o = idx_off + i * 20
    a, b, off, size, ck = struct.unpack_from("<IIIII", d, o)
    raw = d[off:off + size]
    # pad to dwords like the game does ((size+3)/4)
    pad = raw + b"\0" * ((4 - len(raw) % 4) % 4)
    words = list(struct.unpack("<%dI" % (len(pad) // 4), pad))
    print(f"entry {i}: name={a:08x}{b:08x} off={off} size={size} checksum={ck:08x} raw_head={raw[:4].hex()}")
    for init in (0, 1, M, 0x12345678):
        got = h(words, init)
        if got == ck:
            print(f"   MATCH with init={init:#x} over STORED bytes")
    # also try the reverse: hash of the *count* first?
    print(f"   hash(stored, init=0)= {h(words,0):08x}")
