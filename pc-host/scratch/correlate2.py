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
rows = []
for e in range(min(entries, 50)):
    a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off + e*20)
    pos = off
    dobj = zlib.decompressobj(-15)
    total = b""
    nch = 0
    try:
        while len(total) < size and pos + 4 <= idx_off:
            v = struct.unpack_from("<I", d, pos)[0]
            ln = v & 0x7fffffff
            pos += 4
            total += dobj.decompress(d[pos:pos+ln])
            pos += ln
            nch += 1
            if v & 0x80000000:
                break
    except Exception:
        continue
    if len(total) != size:
        continue
    rows.append((fold(total) == ck, size, nch))
small_match = sum(1 for m, s, n in rows if m and s <= 512)
small_miss  = sum(1 for m, s, n in rows if not m and s <= 512)
big_match   = sum(1 for m, s, n in rows if m and s > 512)
big_miss    = sum(1 for m, s, n in rows if not m and s > 512)
print(f"size<=512: match={small_match} miss={small_miss}")
print(f"size>512 : match={big_match} miss={big_miss}")
print("sample matching sizes:", sorted(s for m, s, n in rows if m)[:10])
print("sample mismatch sizes:", sorted(s for m, s, n in rows if not m)[:10])
