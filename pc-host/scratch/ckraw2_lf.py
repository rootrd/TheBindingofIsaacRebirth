import struct
M = 0xFFFFFFFF
def fold(data, init=0xababeb98):
    s = init
    pad = data + b"\0" * ((4 - len(data) % 4) % 4)
    for w in struct.unpack("<%dI" % (len(pad)//4), pad):
        s = (((s << 31) & M) + ((w + (s >> 1)) & M)) & M
    return s
for arch in ("afterbirth", "config", "graphics"):
    P = f"/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/{arch}.a"
    d = open(P, "rb").read()
    idx_off, entries = struct.unpack_from("<IH", d, 8)
    ok = 0; tot = min(entries, 20)
    for e in range(tot):
        a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off + e*20)
        pos = off
        raw = b""
        while pos + 4 <= idx_off:
            v = struct.unpack_from("<I", d, pos)[0]
            ln = v & 0x7fffffff
            raw += d[pos:pos+4+ln]
            pos += 4 + ln
            if v & 0x80000000:
                break
        if fold(raw) == ck: ok += 1
    print(f"{arch}: {ok}/{tot} raw-stream fold matches (init=0xababeb98)")
