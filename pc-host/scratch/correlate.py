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
match_no400 = match_400 = mis_no400 = mis_400 = 0
for e in range(min(entries, 50)):
    a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off + e*20)
    pos = off
    dobj = zlib.decompressobj(-15)
    total = b""
    has400 = False
    try:
        while len(total) < size and pos + 4 <= idx_off:
            v = struct.unpack_from("<I", d, pos)[0]
            ln = v & 0x7fffffff
            pos += 4
            if (v & 0x7fffffff) == 0x400:
                has400 = True
            total += dobj.decompress(d[pos:pos+ln])
            pos += ln
            if v & 0x80000000:
                break
    except Exception:
        continue
    if len(total) != size:
        continue
    m = fold(total) == ck
    if m and not has400: match_no400 += 1
    elif m and has400: match_400 += 1
    elif not m and not has400: mis_no400 += 1
    else: mis_400 += 1
print(f"match&no-400chunk={match_no400}  match&has-400={match_400}")
print(f"miss&no-400chunk={mis_no400}  miss&has-400={mis_400}")
